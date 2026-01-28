//! Main OCPP-ROJ adapter
//!
//! The adapter bridges OCPP 2.0.1 commands from a CSMS to ROJ distributed consensus.
//!
//! ## Architecture
//!
//! ```text
//! OCPP CSMS (Backend)
//!       │ WebSocket JSON-RPC
//!       ▼
//! ┌─────────────────────────────────┐
//! │    OCPP-ROJ Adapter             │
//! │  ┌───────────┐  ┌────────────┐  │
//! │  │ OCPP WS   │◄►│ Translator │  │
//! │  │ Client    │  │ OCPP↔ROJ   │  │
//! │  └───────────┘  └────────────┘  │
//! └─────────────┬───────────────────┘
//!               │ UDP JSON
//!               ▼
//! ┌─────────────────────────────────┐
//! │    ROJ Core (consensus)         │
//! │  Discovery │ Transport │ Voting │
//! └─────────────────────────────────┘
//! ```
//!
//! ## Message Flow
//!
//! 1. CSMS sends SetChargingProfile to adapter
//! 2. Adapter translates to ROJ PROPOSE
//! 3. ROJ peers vote on proposal
//! 4. On COMMIT, adapter updates local state
//! 5. Adapter sends StatusNotification to CSMS

use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;

use roj_core::{Consensus, Discovery, Language, Message as RojMessage, PeerInfo, Transport, Vote};
use serde_json::Value;
use tokio::sync::{mpsc, RwLock};
use tracing::{debug, error, info, warn};

use crate::config::AdapterConfig;
use crate::ocpp::{
    Action, Call, CallResult, GenericStatus, IncomingRequest, OcppClient, OcppClientConfig,
    ReservationStatus, SetChargingProfileRequest, RequestStartTransactionRequest,
    RequestStopTransactionRequest, ReserveNowRequest, CancelReservationRequest,
};
use crate::roj::{
    CommitEffect, OcppToRoj, RojToOcpp, StateBridge,
    translator::{validate_proposal, ProposalValidation},
};

/// The main OCPP-ROJ adapter
pub struct Adapter {
    config: AdapterConfig,

    // ROJ components
    discovery: Option<Discovery>,
    transport: Option<Transport>,
    consensus: Consensus,
    peers: Arc<RwLock<HashMap<String, PeerInfo>>>,

    // Translation layer
    ocpp_to_roj: OcppToRoj,
    roj_to_ocpp: RojToOcpp,
    state_bridge: StateBridge,

    // Shared state
    committed_state: Arc<RwLock<HashMap<String, Value>>>,
}

impl Adapter {
    /// Create a new adapter with the given configuration
    pub async fn new(config: AdapterConfig) -> Result<Self, Box<dyn std::error::Error>> {
        let peers = Arc::new(RwLock::new(HashMap::new()));
        let committed_state = Arc::new(RwLock::new(HashMap::new()));

        // Initialize ROJ consensus
        let consensus = Consensus::new(config.node_id.clone(), peers.clone());

        // Initialize translators
        let ocpp_to_roj = OcppToRoj::new(&config.node_id, &config.ocpp.station_id);
        let roj_to_ocpp = RojToOcpp::new(&config.ocpp.station_id);
        let state_bridge = StateBridge::new(&config.ocpp.station_id, committed_state.clone());

        Ok(Self {
            config,
            discovery: None,
            transport: None,
            consensus,
            peers,
            ocpp_to_roj,
            roj_to_ocpp,
            state_bridge,
            committed_state,
        })
    }

    /// Initialize ROJ networking (discovery and transport)
    pub async fn init_roj(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        info!("Initializing ROJ networking...");

        // Initialize discovery
        if self.config.roj.use_mdns {
            let discovery = Discovery::new(self.config.node_id.clone(), Language::Rust)?;
            discovery.announce()?;
            discovery.browse().await?;

            // Share peers with consensus
            self.peers = discovery.peers();
            self.discovery = Some(discovery);
            info!("mDNS discovery started");
        } else if !self.config.roj.static_peers.is_empty() {
            // Add static peers
            let mut peers = self.peers.write().await;
            for (i, addr) in self.config.roj.static_peers.iter().enumerate() {
                let peer_id = format!("peer-{}", i);
                peers.insert(
                    peer_id.clone(),
                    PeerInfo {
                        node_id: peer_id,
                        lang: Language::Rust,
                        addr: *addr,
                        capabilities: vec!["consensus".to_string()],
                        version: "0.1.0".to_string(),
                        last_seen: std::time::SystemTime::now(),
                    },
                );
            }
            info!("Added {} static peers", self.config.roj.static_peers.len());
        }

        // Initialize transport
        let transport = Transport::new(self.config.roj.port).await?;
        let local_addr = transport.local_addr()?;
        info!("ROJ transport listening on {}", local_addr);

        transport.start_receive();
        self.transport = Some(transport);

        Ok(())
    }

    /// Run the adapter main loop
    pub async fn run(mut self) -> Result<(), Box<dyn std::error::Error>> {
        info!(
            "Starting OCPP-ROJ adapter: node={}, station={}",
            self.config.node_id, self.config.ocpp.station_id
        );

        // Initialize ROJ
        self.init_roj().await?;

        // Create OCPP client
        let ocpp_config = OcppClientConfig {
            csms_url: self.config.ocpp.csms_url.clone(),
            station_id: self.config.ocpp.station_id.clone(),
            vendor: self.config.ocpp.vendor.clone(),
            model: self.config.ocpp.model.clone(),
            serial_number: self.config.ocpp.serial_number.clone(),
            firmware_version: self.config.ocpp.firmware_version.clone(),
            evse_count: self.config.ocpp.evse_count,
            reconnect_delay: self.config.ocpp.reconnect_delay,
            max_reconnect_delay: self.config.ocpp.max_reconnect_delay,
            request_timeout: self.config.ocpp.request_timeout,
        };

        let (ocpp_client, mut ocpp_rx) = OcppClient::new(ocpp_config);
        let ocpp_session = ocpp_client.session();

        // Channel for sending OCPP responses
        let (response_tx, mut response_rx) = mpsc::channel::<(String, CallResult)>(64);

        // Get transport for sending
        let transport = self.transport.take().expect("Transport not initialized");
        let send_transport = Transport::new(self.config.roj.port + 1000).await?;

        // Clone for async tasks
        let node_id = self.config.node_id.clone();
        let peers_for_announce = self.peers.clone();

        // Spawn OCPP connection task
        let (ocpp_incoming_tx, mut ocpp_incoming_rx) = mpsc::channel::<IncomingRequest>(64);
        tokio::spawn(async move {
            if let Err(e) = ocpp_client.run(ocpp_incoming_tx).await {
                error!("OCPP client error: {}", e);
            }
        });

        // Spawn heartbeat announce task
        tokio::spawn(async move {
            let mut interval = tokio::time::interval(Duration::from_secs(1));
            loop {
                interval.tick().await;
                let announce = RojMessage::Announce {
                    node_id: node_id.clone(),
                    lang: Language::Rust,
                    capabilities: vec!["consensus".to_string(), "ocpp".to_string()],
                    version: "0.1.0".to_string(),
                };

                let peers = peers_for_announce.read().await;
                let addrs: Vec<_> = peers.values().map(|p| p.addr).collect();
                drop(peers);

                // Note: Can't easily share transport, using mDNS for announcements
            }
        });

        // Main event loop
        let mut transport = transport;
        info!("Adapter main loop started");

        loop {
            tokio::select! {
                // Handle incoming ROJ messages
                Some((msg, src)) = transport.recv() => {
                    match msg {
                        RojMessage::Announce { node_id, lang, .. } => {
                            if node_id != self.config.node_id {
                                debug!("Received ANNOUNCE from {} ({:?})", node_id, lang);
                                // Update peer last_seen
                                let mut peers = self.peers.write().await;
                                if let Some(peer) = peers.get_mut(&node_id) {
                                    peer.last_seen = std::time::SystemTime::now();
                                }
                            }
                        }

                        RojMessage::Propose { proposal_id, from, key, value, timestamp } => {
                            if from != self.config.node_id {
                                info!("Received PROPOSE {} from {}: {}={}", proposal_id, from, key, value);

                                // Validate proposal
                                let vote = match validate_proposal(&key, &value) {
                                    ProposalValidation::Accept => {
                                        // Record proposal and vote accept
                                        let vote_msg = self.consensus.handle_proposal(
                                            proposal_id.clone(),
                                            from,
                                            key,
                                            value,
                                            timestamp,
                                        );
                                        vote_msg
                                    }
                                    ProposalValidation::Reject(reason) => {
                                        warn!("Rejecting proposal {}: {}", proposal_id, reason);
                                        RojMessage::Vote {
                                            proposal_id: proposal_id.clone(),
                                            from: self.config.node_id.clone(),
                                            vote: Vote::Reject,
                                        }
                                    }
                                };

                                // Send vote back
                                if let Err(e) = send_transport.send(&vote, src).await {
                                    warn!("Failed to send vote: {}", e);
                                }
                            }
                        }

                        RojMessage::Vote { proposal_id, from, vote } => {
                            if from != self.config.node_id {
                                info!("Received VOTE {:?} from {} for {}", vote, from, proposal_id);

                                if let Some(commit_msg) = self.consensus.handle_vote(
                                    proposal_id,
                                    from,
                                    vote,
                                ).await {
                                    // Broadcast commit
                                    let addrs = self.consensus.peer_addresses().await;
                                    if let Err(e) = send_transport.broadcast(&commit_msg, &addrs).await {
                                        warn!("Failed to broadcast commit: {}", e);
                                    }

                                    // Apply commit locally
                                    if let RojMessage::Commit { key, value, .. } = &commit_msg {
                                        self.apply_commit(key, value).await;
                                    }
                                }
                            }
                        }

                        RojMessage::Commit { proposal_id, key, value, voters } => {
                            info!("Received COMMIT {} (voters: {:?})", key, voters);
                            self.consensus.handle_commit(proposal_id, key.clone(), value.clone(), voters);
                            self.apply_commit(&key, &value).await;
                        }
                    }
                }

                // Handle incoming OCPP requests from CSMS
                Some(req) = ocpp_incoming_rx.recv() => {
                    info!("OCPP request: {:?} ({})", req.action, req.message_id);

                    // Translate to ROJ proposal
                    let call = Call {
                        message_id: req.message_id.clone(),
                        action: req.action.clone(),
                        payload: req.payload.clone(),
                    };

                    if let Some(proposal) = self.ocpp_to_roj.translate_call(&call) {
                        // Broadcast proposal to ROJ peers
                        let addrs = self.consensus.peer_addresses().await;

                        if addrs.is_empty() {
                            warn!("No ROJ peers discovered, accepting locally");
                            // Self-commit for single-node operation
                            if let RojMessage::Propose { key, value, .. } = &proposal {
                                self.apply_commit(key, value).await;
                            }
                            // Send immediate response
                            self.send_ocpp_response(&req).await;
                        } else {
                            info!("Broadcasting proposal to {} peers", addrs.len());
                            if let Err(e) = send_transport.broadcast(&proposal, &addrs).await {
                                warn!("Failed to broadcast proposal: {}", e);
                            }

                            // Also vote on our own proposal
                            if let RojMessage::Propose { proposal_id, key, value, timestamp, .. } = proposal {
                                let vote = self.consensus.handle_proposal(
                                    proposal_id,
                                    self.config.node_id.clone(),
                                    key,
                                    value,
                                    timestamp,
                                );
                                // Send vote to all peers
                                let _ = send_transport.broadcast(&vote, &addrs).await;
                            }

                            // Response will be sent after consensus
                            // For now, send immediate acceptance
                            self.send_ocpp_response(&req).await;
                        }
                    } else {
                        // Non-consensus action, respond immediately
                        self.send_ocpp_response(&req).await;
                    }
                }
            }
        }
    }

    /// Apply a committed value to local state
    async fn apply_commit(&mut self, key: &str, value: &Value) {
        info!("Applying commit: {} = {}", key, value);

        // Update committed state
        {
            let mut state = self.committed_state.write().await;
            if value.is_null() {
                state.remove(key);
            } else {
                state.insert(key.to_string(), value.clone());
            }
        }

        // Handle side effects
        if let Some(effect) = self.roj_to_ocpp.handle_commit(key, value) {
            match effect {
                CommitEffect::PowerLimitChanged { evse_id, limit_kw, .. } => {
                    info!("Power limit changed on EVSE {}: {} kW", evse_id, limit_kw);
                    // Could trigger StatusNotification here
                }
                CommitEffect::SessionChanged { evse_id, active, transaction_id } => {
                    info!(
                        "Session {} on EVSE {}: active={}",
                        transaction_id.as_deref().unwrap_or("?"),
                        evse_id,
                        active
                    );
                }
                CommitEffect::ReservationCreated { evse_id, reservation_id } => {
                    info!("Reservation {} created on EVSE {}", reservation_id, evse_id);
                }
                CommitEffect::ReservationCancelled { evse_id } => {
                    info!("Reservation cancelled on EVSE {}", evse_id);
                }
            }
        }
    }

    /// Send OCPP response for a request
    async fn send_ocpp_response(&self, req: &IncomingRequest) {
        // For now, just log - actual response sending requires OCPP client access
        match req.action {
            Action::SetChargingProfile => {
                debug!("Would respond to SetChargingProfile: Accepted");
            }
            Action::RequestStartTransaction => {
                debug!("Would respond to RequestStartTransaction: Accepted");
            }
            Action::RequestStopTransaction => {
                debug!("Would respond to RequestStopTransaction: Accepted");
            }
            Action::ReserveNow => {
                debug!("Would respond to ReserveNow: Accepted");
            }
            Action::CancelReservation => {
                debug!("Would respond to CancelReservation: Accepted");
            }
            _ => {
                debug!("Would respond to {:?}", req.action);
            }
        }
    }

    /// Get current state dump for debugging
    pub async fn dump_state(&self) -> HashMap<String, Value> {
        self.state_bridge.dump_state().await
    }
}

/// Builder for the adapter
pub struct AdapterBuilder {
    config: AdapterConfig,
}

impl AdapterBuilder {
    /// Create a new adapter builder
    pub fn new() -> Self {
        Self {
            config: AdapterConfig::default(),
        }
    }

    /// Set node ID
    pub fn node_id(mut self, id: impl Into<String>) -> Self {
        self.config.node_id = id.into();
        self
    }

    /// Set CSMS URL
    pub fn csms_url(mut self, url: impl Into<String>) -> Self {
        self.config.ocpp.csms_url = url.into();
        self
    }

    /// Set station ID
    pub fn station_id(mut self, id: impl Into<String>) -> Self {
        self.config.ocpp.station_id = id.into();
        self
    }

    /// Set vendor info
    pub fn vendor(mut self, vendor: impl Into<String>, model: impl Into<String>) -> Self {
        self.config.ocpp.vendor = vendor.into();
        self.config.ocpp.model = model.into();
        self
    }

    /// Set ROJ port
    pub fn roj_port(mut self, port: u16) -> Self {
        self.config.roj.port = port;
        self
    }

    /// Build the adapter
    pub async fn build(self) -> Result<Adapter, Box<dyn std::error::Error>> {
        Adapter::new(self.config).await
    }
}

impl Default for AdapterBuilder {
    fn default() -> Self {
        Self::new()
    }
}
