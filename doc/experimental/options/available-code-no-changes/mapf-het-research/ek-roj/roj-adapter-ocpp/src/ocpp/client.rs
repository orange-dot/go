//! OCPP WebSocket client
//!
//! Implements the WebSocket client for connecting to OCPP CSMS (Central System Management System).
//! Handles:
//! - WebSocket connection with OCPP subprotocol
//! - Automatic reconnection with exponential backoff
//! - Request/response correlation
//! - Heartbeat maintenance

use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;

use futures_util::{SinkExt, StreamExt};
use tokio::net::TcpStream;
use tokio::sync::{mpsc, oneshot, RwLock};
use tokio_tungstenite::{
    connect_async_with_config,
    tungstenite::{
        handshake::client::Request,
        http::{header, Uri},
        protocol::WebSocketConfig,
        Message,
    },
    MaybeTlsStream, WebSocketStream,
};
use tracing::{debug, error, info, warn};

use super::messages::*;
use super::session::{Session, SessionEvent};
use super::types::*;

/// OCPP 2.0.1 WebSocket subprotocol
const OCPP_SUBPROTOCOL: &str = "ocpp2.0.1";

/// Configuration for the OCPP client
#[derive(Debug, Clone)]
pub struct OcppClientConfig {
    /// CSMS WebSocket URL
    pub csms_url: String,
    /// Charging station identity (used in URL path)
    pub station_id: String,
    /// Vendor name
    pub vendor: String,
    /// Model name
    pub model: String,
    /// Serial number (optional)
    pub serial_number: Option<String>,
    /// Firmware version (optional)
    pub firmware_version: Option<String>,
    /// Number of EVSEs
    pub evse_count: u32,
    /// Reconnect delay (initial)
    pub reconnect_delay: Duration,
    /// Maximum reconnect delay
    pub max_reconnect_delay: Duration,
    /// Request timeout
    pub request_timeout: Duration,
}

impl Default for OcppClientConfig {
    fn default() -> Self {
        Self {
            csms_url: "ws://localhost:8180/steve/websocket/CentralSystemService".to_string(),
            station_id: "EK3-001".to_string(),
            vendor: "Elektrokombinacija".to_string(),
            model: "EK3-OCPP".to_string(),
            serial_number: None,
            firmware_version: Some("0.1.0".to_string()),
            evse_count: 1,
            reconnect_delay: Duration::from_secs(5),
            max_reconnect_delay: Duration::from_secs(300),
            request_timeout: Duration::from_secs(30),
        }
    }
}

/// Pending request awaiting response
struct PendingRequest {
    action: Action,
    response_tx: oneshot::Sender<Result<CallResult, OcppError>>,
}

/// Incoming CSMS request for the adapter to handle
#[derive(Debug)]
pub struct IncomingRequest {
    pub message_id: String,
    pub action: Action,
    pub payload: serde_json::Value,
}

/// OCPP WebSocket client
pub struct OcppClient {
    config: OcppClientConfig,
    session: Arc<RwLock<Session>>,
    pending_requests: Arc<RwLock<HashMap<String, PendingRequest>>>,

    // Channels for communication
    outgoing_tx: mpsc::Sender<OcppMessage>,
    incoming_rx: mpsc::Receiver<IncomingRequest>,
}

impl OcppClient {
    /// Create a new OCPP client
    pub fn new(config: OcppClientConfig) -> (Self, mpsc::Receiver<IncomingRequest>) {
        let mut session = Session::new(
            &config.station_id,
            &config.vendor,
            &config.model,
        );

        if let Some(ref serial) = config.serial_number {
            session = session.with_serial(serial);
        }
        if let Some(ref fw) = config.firmware_version {
            session = session.with_firmware(fw);
        }

        // Add EVSEs
        for i in 1..=config.evse_count {
            session.add_evse(i as i32, 1);
        }

        let (outgoing_tx, outgoing_rx) = mpsc::channel(64);
        let (incoming_tx, incoming_rx) = mpsc::channel(64);
        let (client_incoming_tx, client_incoming_rx) = mpsc::channel(64);

        let client = Self {
            config,
            session: Arc::new(RwLock::new(session)),
            pending_requests: Arc::new(RwLock::new(HashMap::new())),
            outgoing_tx,
            incoming_rx: client_incoming_rx,
        };

        // The adapter receives incoming requests through this channel
        (client, incoming_rx)
    }

    /// Get a reference to the session
    pub fn session(&self) -> Arc<RwLock<Session>> {
        self.session.clone()
    }

    /// Send a request and wait for response
    pub async fn request(&self, call: Call) -> Result<CallResult, OcppError> {
        let (response_tx, response_rx) = oneshot::channel();

        // Track pending request
        {
            let mut pending = self.pending_requests.write().await;
            pending.insert(
                call.message_id.clone(),
                PendingRequest {
                    action: call.action.clone(),
                    response_tx,
                },
            );
        }

        // Send the request
        self.outgoing_tx
            .send(OcppMessage::Call(call))
            .await
            .map_err(|_| OcppError::ConnectionClosed)?;

        // Wait for response with timeout
        match tokio::time::timeout(self.config.request_timeout, response_rx).await {
            Ok(Ok(result)) => result,
            Ok(Err(_)) => Err(OcppError::ConnectionClosed),
            Err(_) => Err(OcppError::Timeout),
        }
    }

    /// Send a response to a CSMS request
    pub async fn respond(&self, response: CallResult) -> Result<(), OcppError> {
        self.outgoing_tx
            .send(OcppMessage::CallResult(response))
            .await
            .map_err(|_| OcppError::ConnectionClosed)
    }

    /// Send an error response to a CSMS request
    pub async fn respond_error(&self, error: CallError) -> Result<(), OcppError> {
        self.outgoing_tx
            .send(OcppMessage::CallError(error))
            .await
            .map_err(|_| OcppError::ConnectionClosed)
    }

    /// Send BootNotification
    pub async fn boot_notification(&self) -> Result<BootNotificationResponse, OcppError> {
        let session = self.session.read().await;
        let call = Call::boot_notification(
            session.charging_station_info(),
            BootReason::PowerUp,
        )?;
        drop(session);

        let result = self.request(call).await?;
        let response: BootNotificationResponse = result.parse_payload()?;

        // Update session state
        let mut session = self.session.write().await;
        match response.status {
            RegistrationStatus::Accepted => {
                session.handle_event(SessionEvent::BootAccepted {
                    interval: response.interval,
                });
            }
            RegistrationStatus::Pending => {
                session.handle_event(SessionEvent::BootPending {
                    interval: response.interval,
                });
            }
            RegistrationStatus::Rejected => {
                session.handle_event(SessionEvent::BootRejected);
            }
        }

        Ok(response)
    }

    /// Send Heartbeat
    pub async fn heartbeat(&self) -> Result<HeartbeatResponse, OcppError> {
        let call = Call::heartbeat()?;
        let result = self.request(call).await?;
        let response: HeartbeatResponse = result.parse_payload()?;

        let mut session = self.session.write().await;
        session.handle_event(SessionEvent::HeartbeatDue);

        Ok(response)
    }

    /// Send StatusNotification for all EVSEs
    pub async fn status_notification_all(&self) -> Result<(), OcppError> {
        let session = self.session.read().await;
        let statuses = session.evse_statuses();
        drop(session);

        for (evse_id, connector_id, status) in statuses {
            let call = Call::status_notification(evse_id, connector_id, status)?;
            let _ = self.request(call).await?;
        }

        Ok(())
    }

    /// Run the client connection loop
    pub async fn run(
        self,
        mut incoming_tx: mpsc::Sender<IncomingRequest>,
    ) -> Result<(), OcppError> {
        let mut reconnect_delay = self.config.reconnect_delay;

        loop {
            info!("Connecting to CSMS: {}", self.config.csms_url);

            match self.connect_and_run(&mut incoming_tx).await {
                Ok(()) => {
                    info!("Connection closed gracefully");
                    break Ok(());
                }
                Err(e) => {
                    error!("Connection error: {}", e);

                    {
                        let mut session = self.session.write().await;
                        session.handle_event(SessionEvent::Disconnected);
                    }

                    // Exponential backoff
                    info!("Reconnecting in {:?}", reconnect_delay);
                    tokio::time::sleep(reconnect_delay).await;
                    reconnect_delay = std::cmp::min(
                        reconnect_delay * 2,
                        self.config.max_reconnect_delay,
                    );
                }
            }
        }
    }

    /// Connect and run until disconnection
    async fn connect_and_run(
        &self,
        incoming_tx: &mut mpsc::Sender<IncomingRequest>,
    ) -> Result<(), OcppError> {
        // Build WebSocket URL with station ID
        let url = format!("{}/{}", self.config.csms_url, self.config.station_id);
        let uri: Uri = url.parse().map_err(|_| OcppError::InvalidFormat)?;

        // Build request with OCPP subprotocol
        let request = Request::builder()
            .uri(&url)
            .header(header::SEC_WEBSOCKET_PROTOCOL, OCPP_SUBPROTOCOL)
            .header(header::HOST, uri.host().unwrap_or("localhost"))
            .body(())
            .map_err(|_| OcppError::InvalidFormat)?;

        // Connect
        let ws_config = WebSocketConfig {
            max_message_size: Some(64 * 1024),
            max_frame_size: Some(16 * 1024),
            ..Default::default()
        };

        let (ws_stream, response) = connect_async_with_config(request, Some(ws_config), false)
            .await
            .map_err(|e| {
                error!("WebSocket connection failed: {}", e);
                OcppError::ConnectionClosed
            })?;

        // Verify subprotocol
        let accepted_protocol = response
            .headers()
            .get(header::SEC_WEBSOCKET_PROTOCOL)
            .and_then(|v| v.to_str().ok());

        if accepted_protocol != Some(OCPP_SUBPROTOCOL) {
            warn!(
                "CSMS did not accept OCPP 2.0.1 subprotocol, got: {:?}",
                accepted_protocol
            );
        }

        info!("WebSocket connected to {}", url);

        {
            let mut session = self.session.write().await;
            session.handle_event(SessionEvent::Connected);
        }

        // Split the stream
        let (mut ws_tx, mut ws_rx) = ws_stream.split();

        // Create channels for internal communication
        let (send_tx, mut send_rx) = mpsc::channel::<OcppMessage>(64);

        // Spawn sender task
        let sender_handle = tokio::spawn(async move {
            while let Some(msg) = send_rx.recv().await {
                let bytes = match msg.to_bytes() {
                    Ok(b) => b,
                    Err(e) => {
                        error!("Failed to serialize message: {}", e);
                        continue;
                    }
                };

                debug!("Sending: {}", String::from_utf8_lossy(&bytes));

                if let Err(e) = ws_tx.send(Message::Text(String::from_utf8_lossy(&bytes).into_owned().into())).await {
                    error!("Failed to send WebSocket message: {}", e);
                    break;
                }
            }
        });

        // Send BootNotification
        let boot_call = Call::boot_notification(
            self.session.read().await.charging_station_info(),
            BootReason::PowerUp,
        )?;
        send_tx.send(OcppMessage::Call(boot_call.clone())).await
            .map_err(|_| OcppError::ConnectionClosed)?;

        // Track the boot request
        {
            let mut session = self.session.write().await;
            session.track_request(boot_call.message_id.clone(), "BootNotification".into());
        }

        // Main receive loop
        let pending = self.pending_requests.clone();
        let session = self.session.clone();

        loop {
            tokio::select! {
                // Receive from WebSocket
                msg = ws_rx.next() => {
                    match msg {
                        Some(Ok(Message::Text(text))) => {
                            debug!("Received: {}", text);

                            match OcppMessage::parse(text.as_bytes()) {
                                Ok(OcppMessage::Call(call)) => {
                                    // CSMS request - forward to adapter
                                    if let Err(e) = incoming_tx.send(IncomingRequest {
                                        message_id: call.message_id,
                                        action: call.action,
                                        payload: call.payload,
                                    }).await {
                                        error!("Failed to forward CSMS request: {}", e);
                                    }
                                }
                                Ok(OcppMessage::CallResult(result)) => {
                                    // Response to our request
                                    let mut pending = pending.write().await;
                                    if let Some(req) = pending.remove(&result.message_id) {
                                        // Handle BootNotification response specially
                                        if req.action == Action::BootNotification {
                                            if let Ok(boot_resp) = serde_json::from_value::<BootNotificationResponse>(result.payload.clone()) {
                                                let mut sess = session.write().await;
                                                match boot_resp.status {
                                                    RegistrationStatus::Accepted => {
                                                        sess.handle_event(SessionEvent::BootAccepted {
                                                            interval: boot_resp.interval,
                                                        });
                                                    }
                                                    RegistrationStatus::Pending => {
                                                        sess.handle_event(SessionEvent::BootPending {
                                                            interval: boot_resp.interval,
                                                        });
                                                    }
                                                    RegistrationStatus::Rejected => {
                                                        sess.handle_event(SessionEvent::BootRejected);
                                                    }
                                                }
                                            }
                                        }
                                        let _ = req.response_tx.send(Ok(result));
                                    }
                                }
                                Ok(OcppMessage::CallError(error)) => {
                                    // Error response to our request
                                    let mut pending = pending.write().await;
                                    if let Some(req) = pending.remove(&error.message_id) {
                                        let _ = req.response_tx.send(Err(OcppError::RemoteError {
                                            code: error.error_code,
                                            description: error.error_description,
                                            details: error.error_details,
                                        }));
                                    }
                                }
                                Err(e) => {
                                    warn!("Failed to parse OCPP message: {}", e);
                                }
                            }
                        }
                        Some(Ok(Message::Close(_))) => {
                            info!("WebSocket closed by server");
                            break;
                        }
                        Some(Ok(Message::Ping(data))) => {
                            // Respond with pong (handled automatically by tungstenite)
                            debug!("Received ping");
                        }
                        Some(Err(e)) => {
                            error!("WebSocket error: {}", e);
                            break;
                        }
                        None => {
                            info!("WebSocket stream ended");
                            break;
                        }
                        _ => {}
                    }
                }

                // Check for heartbeat
                _ = tokio::time::sleep(Duration::from_secs(1)) => {
                    let sess = session.read().await;
                    if sess.heartbeat_due() {
                        drop(sess);
                        if let Ok(call) = Call::heartbeat() {
                            let _ = send_tx.send(OcppMessage::Call(call)).await;
                        }
                    }
                }
            }
        }

        sender_handle.abort();
        Ok(())
    }
}

/// Build the full OCPP WebSocket URL
pub fn build_ocpp_url(base_url: &str, station_id: &str) -> String {
    format!("{}/{}", base_url.trim_end_matches('/'), station_id)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_build_ocpp_url() {
        let url = build_ocpp_url("ws://localhost:8180/steve/websocket/CentralSystemService", "EK3-001");
        assert_eq!(
            url,
            "ws://localhost:8180/steve/websocket/CentralSystemService/EK3-001"
        );

        let url = build_ocpp_url("ws://localhost:8180/steve/websocket/CentralSystemService/", "EK3-001");
        assert_eq!(
            url,
            "ws://localhost:8180/steve/websocket/CentralSystemService/EK3-001"
        );
    }
}
