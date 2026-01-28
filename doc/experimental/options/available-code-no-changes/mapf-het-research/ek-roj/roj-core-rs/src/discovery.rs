//! mDNS discovery for ROJ nodes

use crate::types::{Language, NodeId, PeerInfo};
use mdns_sd::{ServiceDaemon, ServiceEvent, ServiceInfo};
use std::collections::HashMap;
use std::net::{IpAddr, SocketAddr};
use std::sync::Arc;
use std::time::SystemTime;
use tokio::sync::RwLock;
use tracing::{debug, info, warn};

/// mDNS service type for ROJ protocol
const SERVICE_TYPE: &str = "_roj._udp.local.";
const UDP_PORT: u16 = 9990;

/// Discovery service for finding ROJ peers
pub struct Discovery {
    node_id: NodeId,
    lang: Language,
    daemon: ServiceDaemon,
    peers: Arc<RwLock<HashMap<NodeId, PeerInfo>>>,
}

impl Discovery {
    /// Create a new discovery service
    pub fn new(node_id: NodeId, lang: Language) -> Result<Self, mdns_sd::Error> {
        let daemon = ServiceDaemon::new()?;

        Ok(Self {
            node_id,
            lang,
            daemon,
            peers: Arc::new(RwLock::new(HashMap::new())),
        })
    }

    /// Get shared reference to peers map
    pub fn peers(&self) -> Arc<RwLock<HashMap<NodeId, PeerInfo>>> {
        self.peers.clone()
    }

    /// Announce this node on the network
    pub fn announce(&self) -> Result<(), mdns_sd::Error> {
        let host = format!("{}.local.", self.node_id);
        let service_name = format!("{}._roj._udp.local.", self.node_id);

        let mut properties = HashMap::new();
        properties.insert("lang".to_string(), self.lang.to_string());
        properties.insert("version".to_string(), "0.1.0".to_string());
        properties.insert("caps".to_string(), "consensus".to_string());

        let service_info = ServiceInfo::new(
            SERVICE_TYPE,
            &self.node_id,
            &host,
            (),
            UDP_PORT,
            properties,
        )?;

        self.daemon.register(service_info)?;
        info!("mDNS: Announcing {} on {}", self.node_id, SERVICE_TYPE);
        Ok(())
    }

    /// Start browsing for other ROJ nodes
    pub async fn browse(&self) -> Result<(), mdns_sd::Error> {
        let receiver = self.daemon.browse(SERVICE_TYPE)?;
        let peers = self.peers.clone();
        let my_id = self.node_id.clone();

        tokio::spawn(async move {
            loop {
                match receiver.recv_async().await {
                    Ok(event) => {
                        Self::handle_event(&my_id, &peers, event).await;
                    }
                    Err(e) => {
                        warn!("mDNS browse error: {}", e);
                        break;
                    }
                }
            }
        });

        Ok(())
    }

    async fn handle_event(
        my_id: &str,
        peers: &Arc<RwLock<HashMap<NodeId, PeerInfo>>>,
        event: ServiceEvent,
    ) {
        match event {
            ServiceEvent::ServiceResolved(info) => {
                let node_id = info.get_fullname().split('.').next().unwrap_or("").to_string();

                // Don't add ourselves
                if node_id == my_id {
                    return;
                }

                let lang = info
                    .get_properties()
                    .get("lang")
                    .and_then(|v| v.val_str().parse().ok())
                    .unwrap_or(Language::Rust);

                let version = info
                    .get_properties()
                    .get("version")
                    .map(|v| v.val_str().to_string())
                    .unwrap_or_else(|| "0.1.0".to_string());

                let capabilities = info
                    .get_properties()
                    .get("caps")
                    .map(|v| v.val_str().split(',').map(String::from).collect())
                    .unwrap_or_else(|| vec!["consensus".to_string()]);

                // Get first IPv4 address
                let addr = info
                    .get_addresses()
                    .iter()
                    .find(|a| matches!(a, IpAddr::V4(_)))
                    .copied()
                    .unwrap_or(IpAddr::V4(std::net::Ipv4Addr::LOCALHOST));

                let peer_info = PeerInfo {
                    node_id: node_id.clone(),
                    lang,
                    addr: SocketAddr::new(addr, info.get_port()),
                    capabilities,
                    version,
                    last_seen: SystemTime::now(),
                };

                info!(
                    "mDNS: Discovered \"{}\" ({}) at {}",
                    node_id, lang, peer_info.addr
                );

                peers.write().await.insert(node_id, peer_info);
            }
            ServiceEvent::ServiceRemoved(_, fullname) => {
                let node_id = fullname.split('.').next().unwrap_or("").to_string();
                if peers.write().await.remove(&node_id).is_some() {
                    debug!("mDNS: Node \"{}\" removed", node_id);
                }
            }
            _ => {}
        }
    }

    /// Shutdown the discovery service
    pub fn shutdown(&self) -> Result<(), mdns_sd::Error> {
        self.daemon.shutdown().map(|_| ())
    }
}

impl std::str::FromStr for Language {
    type Err = ();

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_lowercase().as_str() {
            "rust" => Ok(Language::Rust),
            "go" => Ok(Language::Go),
            "c" => Ok(Language::C),
            _ => Err(()),
        }
    }
}
