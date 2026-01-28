//! UDP transport for ROJ messages

use crate::types::Message;
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::net::UdpSocket;
use tokio::sync::mpsc;
use tracing::{debug, error, warn};

/// Default UDP port for ROJ protocol
pub const DEFAULT_PORT: u16 = 9990;

/// Maximum message size (64KB should be plenty for JSON)
const MAX_MSG_SIZE: usize = 65536;

/// UDP transport for sending and receiving ROJ messages
pub struct Transport {
    socket: Arc<UdpSocket>,
    incoming_tx: mpsc::Sender<(Message, SocketAddr)>,
    incoming_rx: mpsc::Receiver<(Message, SocketAddr)>,
}

impl Transport {
    /// Create a new transport bound to the specified port
    pub async fn new(port: u16) -> Result<Self, std::io::Error> {
        let addr = format!("0.0.0.0:{}", port);
        let socket = UdpSocket::bind(&addr).await?;
        socket.set_broadcast(true)?;

        let (incoming_tx, incoming_rx) = mpsc::channel(256);

        Ok(Self {
            socket: Arc::new(socket),
            incoming_tx,
            incoming_rx,
        })
    }

    /// Get the local address this transport is bound to
    pub fn local_addr(&self) -> Result<SocketAddr, std::io::Error> {
        self.socket.local_addr()
    }

    /// Start receiving messages in background
    pub fn start_receive(&self) {
        let socket = self.socket.clone();
        let tx = self.incoming_tx.clone();

        tokio::spawn(async move {
            let mut buf = vec![0u8; MAX_MSG_SIZE];

            loop {
                match socket.recv_from(&mut buf).await {
                    Ok((len, src)) => {
                        match Message::from_bytes(&buf[..len]) {
                            Ok(msg) => {
                                debug!("Received {:?} from {}", msg, src);
                                if tx.send((msg, src)).await.is_err() {
                                    break;
                                }
                            }
                            Err(e) => {
                                warn!("Failed to parse message from {}: {}", src, e);
                            }
                        }
                    }
                    Err(e) => {
                        error!("UDP receive error: {}", e);
                    }
                }
            }
        });
    }

    /// Receive the next incoming message
    pub async fn recv(&mut self) -> Option<(Message, SocketAddr)> {
        self.incoming_rx.recv().await
    }

    /// Send a message to a specific address
    pub async fn send(&self, msg: &Message, addr: SocketAddr) -> Result<(), std::io::Error> {
        let bytes = msg.to_bytes().map_err(|e| {
            std::io::Error::new(std::io::ErrorKind::InvalidData, e)
        })?;

        debug!("Sending {:?} to {}", msg, addr);
        self.socket.send_to(&bytes, addr).await?;
        Ok(())
    }

    /// Broadcast a message to multiple addresses
    pub async fn broadcast(&self, msg: &Message, addrs: &[SocketAddr]) -> Result<(), std::io::Error> {
        let bytes = msg.to_bytes().map_err(|e| {
            std::io::Error::new(std::io::ErrorKind::InvalidData, e)
        })?;

        for addr in addrs {
            debug!("Broadcasting {:?} to {}", msg, addr);
            if let Err(e) = self.socket.send_to(&bytes, addr).await {
                warn!("Failed to send to {}: {}", addr, e);
            }
        }

        Ok(())
    }

    /// Send a message to the broadcast address
    pub async fn broadcast_local(&self, msg: &Message, port: u16) -> Result<(), std::io::Error> {
        let bytes = msg.to_bytes().map_err(|e| {
            std::io::Error::new(std::io::ErrorKind::InvalidData, e)
        })?;

        let broadcast_addr: SocketAddr = format!("255.255.255.255:{}", port).parse().unwrap();
        debug!("Broadcasting {:?} to {}", msg, broadcast_addr);
        self.socket.send_to(&bytes, broadcast_addr).await?;
        Ok(())
    }
}
