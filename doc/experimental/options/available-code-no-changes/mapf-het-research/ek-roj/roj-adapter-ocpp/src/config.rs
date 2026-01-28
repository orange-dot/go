//! Configuration for the OCPP-ROJ adapter
//!
//! Combines settings for both OCPP client and ROJ participation.

use std::net::SocketAddr;
use std::time::Duration;

/// Complete adapter configuration
#[derive(Debug, Clone)]
pub struct AdapterConfig {
    // Node identity
    pub node_id: String,

    // OCPP settings
    pub ocpp: OcppConfig,

    // ROJ settings
    pub roj: RojConfig,
}

/// OCPP-specific configuration
#[derive(Debug, Clone)]
pub struct OcppConfig {
    /// CSMS WebSocket URL (without station ID)
    pub csms_url: String,

    /// Station ID (appended to URL)
    pub station_id: String,

    /// Vendor name for BootNotification
    pub vendor: String,

    /// Model name for BootNotification
    pub model: String,

    /// Serial number (optional)
    pub serial_number: Option<String>,

    /// Firmware version (optional)
    pub firmware_version: Option<String>,

    /// Number of EVSEs to report
    pub evse_count: u32,

    /// Initial reconnect delay
    pub reconnect_delay: Duration,

    /// Maximum reconnect delay (exponential backoff cap)
    pub max_reconnect_delay: Duration,

    /// Request timeout
    pub request_timeout: Duration,
}

/// ROJ-specific configuration
#[derive(Debug, Clone)]
pub struct RojConfig {
    /// UDP port for ROJ transport
    pub port: u16,

    /// Whether to use mDNS for discovery
    pub use_mdns: bool,

    /// Static peer addresses (if not using mDNS)
    pub static_peers: Vec<SocketAddr>,

    /// Announce interval for heartbeats
    pub announce_interval: Duration,

    /// Proposal timeout
    pub proposal_timeout: Duration,
}

impl Default for AdapterConfig {
    fn default() -> Self {
        Self {
            node_id: "ocpp-adapter".to_string(),
            ocpp: OcppConfig::default(),
            roj: RojConfig::default(),
        }
    }
}

impl Default for OcppConfig {
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

impl Default for RojConfig {
    fn default() -> Self {
        Self {
            port: 9990,
            use_mdns: true,
            static_peers: Vec::new(),
            announce_interval: Duration::from_secs(1),
            proposal_timeout: Duration::from_secs(10),
        }
    }
}

impl AdapterConfig {
    /// Create config with basic parameters
    pub fn new(
        node_id: impl Into<String>,
        station_id: impl Into<String>,
        csms_url: impl Into<String>,
    ) -> Self {
        let node_id = node_id.into();
        let station_id = station_id.into();

        Self {
            node_id: node_id.clone(),
            ocpp: OcppConfig {
                csms_url: csms_url.into(),
                station_id,
                ..Default::default()
            },
            roj: RojConfig::default(),
        }
    }

    /// Set vendor info
    pub fn with_vendor(mut self, vendor: impl Into<String>, model: impl Into<String>) -> Self {
        self.ocpp.vendor = vendor.into();
        self.ocpp.model = model.into();
        self
    }

    /// Set serial number
    pub fn with_serial(mut self, serial: impl Into<String>) -> Self {
        self.ocpp.serial_number = Some(serial.into());
        self
    }

    /// Set firmware version
    pub fn with_firmware(mut self, version: impl Into<String>) -> Self {
        self.ocpp.firmware_version = Some(version.into());
        self
    }

    /// Set EVSE count
    pub fn with_evse_count(mut self, count: u32) -> Self {
        self.ocpp.evse_count = count;
        self
    }

    /// Set ROJ port
    pub fn with_roj_port(mut self, port: u16) -> Self {
        self.roj.port = port;
        self
    }

    /// Add static ROJ peer
    pub fn with_peer(mut self, addr: SocketAddr) -> Self {
        self.roj.static_peers.push(addr);
        self
    }

    /// Disable mDNS discovery
    pub fn without_mdns(mut self) -> Self {
        self.roj.use_mdns = false;
        self
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_config_builder() {
        let config = AdapterConfig::new("alpha", "CS001", "ws://localhost:8180/ocpp")
            .with_vendor("EK", "EK3")
            .with_serial("SN001")
            .with_evse_count(2)
            .with_roj_port(9991);

        assert_eq!(config.node_id, "alpha");
        assert_eq!(config.ocpp.station_id, "CS001");
        assert_eq!(config.ocpp.vendor, "EK");
        assert_eq!(config.ocpp.evse_count, 2);
        assert_eq!(config.roj.port, 9991);
    }
}
