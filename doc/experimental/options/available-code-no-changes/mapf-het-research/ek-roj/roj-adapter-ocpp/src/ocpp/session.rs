//! OCPP session state machine
//!
//! Manages the lifecycle of a charging point's connection to the CSMS:
//! - Boot sequence (BootNotification → registration)
//! - Heartbeat maintenance
//! - Transaction lifecycle
//! - Status transitions

use std::collections::HashMap;
use chrono::{DateTime, Utc};
use tracing::{info, warn, debug};

use super::types::*;

/// Session state in the OCPP connection lifecycle
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SessionState {
    /// Initial state, waiting to connect
    Disconnected,
    /// Connected but not registered
    Connected,
    /// BootNotification sent, awaiting response
    BootPending,
    /// Registered with CSMS
    Registered,
    /// Registration rejected, will retry
    Rejected,
}

/// Events that can occur in the session
#[derive(Debug, Clone)]
pub enum SessionEvent {
    /// WebSocket connected
    Connected,
    /// WebSocket disconnected
    Disconnected,
    /// BootNotification accepted
    BootAccepted { interval: i32 },
    /// BootNotification pending (retry later)
    BootPending { interval: i32 },
    /// BootNotification rejected
    BootRejected,
    /// Heartbeat due
    HeartbeatDue,
    /// Received CSMS request
    CsmsRequest { action: String, message_id: String },
}

/// EVSE (Electric Vehicle Supply Equipment) state
#[derive(Debug, Clone)]
pub struct EvseSessionState {
    pub evse_id: i32,
    pub connector_id: i32,
    pub status: ConnectorStatus,
    pub transaction: Option<TransactionState>,
    pub reservation: Option<ActiveReservation>,
    pub charging_profiles: Vec<ChargingProfile>,
}

impl EvseSessionState {
    pub fn new(evse_id: i32, connector_id: i32) -> Self {
        Self {
            evse_id,
            connector_id,
            status: ConnectorStatus::Available,
            transaction: None,
            reservation: None,
            charging_profiles: Vec::new(),
        }
    }

    /// Get the active power limit (from highest priority profile)
    pub fn active_power_limit_kw(&self) -> Option<f64> {
        // Find highest stack level profile
        let profile = self.charging_profiles
            .iter()
            .max_by_key(|p| p.stack_level)?;

        // Get current period limit
        profile.charging_schedule.first()
            .and_then(|schedule| schedule.charging_schedule_period.first())
            .map(|period| {
                match profile.charging_schedule[0].charging_rate_unit {
                    ChargingRateUnit::W => period.limit / 1000.0,
                    ChargingRateUnit::A => period.limit * 230.0 * 3.0 / 1000.0, // 3-phase estimate
                }
            })
    }
}

/// Active transaction state
#[derive(Debug, Clone)]
pub struct TransactionState {
    pub transaction_id: String,
    pub remote_start_id: Option<i32>,
    pub id_token: String,
    pub started_at: DateTime<Utc>,
    pub meter_start: f64,
    pub meter_current: f64,
}

/// Active reservation
#[derive(Debug, Clone)]
pub struct ActiveReservation {
    pub reservation_id: i32,
    pub id_token: String,
    pub expiry: DateTime<Utc>,
}

/// Main session manager
#[derive(Debug)]
pub struct Session {
    /// Station identity
    pub station_id: String,
    pub vendor: String,
    pub model: String,
    pub serial_number: Option<String>,
    pub firmware_version: Option<String>,

    /// Connection state
    pub state: SessionState,
    pub registered_at: Option<DateTime<Utc>>,
    pub heartbeat_interval: i32,
    pub last_heartbeat: Option<DateTime<Utc>>,

    /// EVSE states (evse_id -> state)
    pub evses: HashMap<i32, EvseSessionState>,

    /// Pending requests (message_id -> action)
    pending_requests: HashMap<String, String>,
}

impl Session {
    /// Create a new session
    pub fn new(
        station_id: impl Into<String>,
        vendor: impl Into<String>,
        model: impl Into<String>,
    ) -> Self {
        Self {
            station_id: station_id.into(),
            vendor: vendor.into(),
            model: model.into(),
            serial_number: None,
            firmware_version: None,
            state: SessionState::Disconnected,
            registered_at: None,
            heartbeat_interval: 300, // Default 5 minutes
            last_heartbeat: None,
            evses: HashMap::new(),
            pending_requests: HashMap::new(),
        }
    }

    /// Set optional station info
    pub fn with_serial(mut self, serial: impl Into<String>) -> Self {
        self.serial_number = Some(serial.into());
        self
    }

    pub fn with_firmware(mut self, version: impl Into<String>) -> Self {
        self.firmware_version = Some(version.into());
        self
    }

    /// Add an EVSE to manage
    pub fn add_evse(&mut self, evse_id: i32, connector_id: i32) {
        self.evses.insert(evse_id, EvseSessionState::new(evse_id, connector_id));
    }

    /// Handle a session event
    pub fn handle_event(&mut self, event: SessionEvent) {
        debug!("Session event: {:?}", event);

        match event {
            SessionEvent::Connected => {
                self.state = SessionState::Connected;
                info!("Session connected, will send BootNotification");
            }

            SessionEvent::Disconnected => {
                self.state = SessionState::Disconnected;
                self.registered_at = None;
                warn!("Session disconnected");
            }

            SessionEvent::BootAccepted { interval } => {
                self.state = SessionState::Registered;
                self.registered_at = Some(Utc::now());
                self.heartbeat_interval = interval;
                info!("Session registered, heartbeat interval: {}s", interval);
            }

            SessionEvent::BootPending { interval } => {
                self.state = SessionState::BootPending;
                self.heartbeat_interval = interval;
                info!("Boot pending, will retry in {}s", interval);
            }

            SessionEvent::BootRejected => {
                self.state = SessionState::Rejected;
                warn!("Boot rejected by CSMS");
            }

            SessionEvent::HeartbeatDue => {
                self.last_heartbeat = Some(Utc::now());
            }

            SessionEvent::CsmsRequest { action, message_id } => {
                debug!("CSMS request: {} ({})", action, message_id);
            }
        }
    }

    /// Check if heartbeat is due
    pub fn heartbeat_due(&self) -> bool {
        if self.state != SessionState::Registered {
            return false;
        }

        match self.last_heartbeat {
            None => true,
            Some(last) => {
                let elapsed = Utc::now().signed_duration_since(last);
                elapsed.num_seconds() >= self.heartbeat_interval as i64
            }
        }
    }

    /// Check if we should retry boot
    pub fn boot_retry_due(&self) -> bool {
        matches!(self.state, SessionState::BootPending | SessionState::Rejected)
    }

    /// Get ChargingStationInfo for BootNotification
    pub fn charging_station_info(&self) -> ChargingStationInfo {
        ChargingStationInfo {
            model: self.model.clone(),
            vendor_name: self.vendor.clone(),
            serial_number: self.serial_number.clone(),
            firmware_version: self.firmware_version.clone(),
        }
    }

    /// Track a pending request
    pub fn track_request(&mut self, message_id: String, action: String) {
        self.pending_requests.insert(message_id, action);
    }

    /// Complete a pending request
    pub fn complete_request(&mut self, message_id: &str) -> Option<String> {
        self.pending_requests.remove(message_id)
    }

    /// Handle SetChargingProfile
    pub fn set_charging_profile(&mut self, evse_id: i32, profile: ChargingProfile) -> GenericStatus {
        if let Some(evse) = self.evses.get_mut(&evse_id) {
            // Remove existing profile with same ID
            evse.charging_profiles.retain(|p| p.id != profile.id);
            evse.charging_profiles.push(profile.clone());

            info!(
                "Set charging profile {} on EVSE {}, limit: {:?} kW",
                profile.id,
                evse_id,
                evse.active_power_limit_kw()
            );

            GenericStatus::Accepted
        } else if evse_id == 0 {
            // Apply to all EVSEs
            for evse in self.evses.values_mut() {
                evse.charging_profiles.retain(|p| p.id != profile.id);
                evse.charging_profiles.push(profile.clone());
            }
            info!("Set charging profile {} on all EVSEs", profile.id);
            GenericStatus::Accepted
        } else {
            warn!("Unknown EVSE {}", evse_id);
            GenericStatus::Rejected
        }
    }

    /// Handle RequestStartTransaction
    pub fn start_transaction(
        &mut self,
        evse_id: Option<i32>,
        id_token: String,
        remote_start_id: i32,
    ) -> (GenericStatus, Option<String>) {
        // Find available EVSE
        let evse_id = evse_id.or_else(|| {
            self.evses.iter()
                .find(|(_, e)| e.status == ConnectorStatus::Available && e.transaction.is_none())
                .map(|(id, _)| *id)
        });

        if let Some(evse_id) = evse_id {
            if let Some(evse) = self.evses.get_mut(&evse_id) {
                if evse.transaction.is_some() {
                    return (GenericStatus::Rejected, None);
                }

                let transaction_id = uuid::Uuid::new_v4().to_string()[..8].to_string();

                evse.transaction = Some(TransactionState {
                    transaction_id: transaction_id.clone(),
                    remote_start_id: Some(remote_start_id),
                    id_token: id_token.clone(),
                    started_at: Utc::now(),
                    meter_start: 0.0,
                    meter_current: 0.0,
                });
                evse.status = ConnectorStatus::Occupied;

                info!(
                    "Started transaction {} on EVSE {} for token {}",
                    transaction_id, evse_id, id_token
                );

                return (GenericStatus::Accepted, Some(transaction_id));
            }
        }

        (GenericStatus::Rejected, None)
    }

    /// Handle RequestStopTransaction
    pub fn stop_transaction(&mut self, transaction_id: &str) -> GenericStatus {
        for evse in self.evses.values_mut() {
            if let Some(ref tx) = evse.transaction {
                if tx.transaction_id == transaction_id {
                    info!(
                        "Stopped transaction {} on EVSE {}, energy: {} kWh",
                        transaction_id,
                        evse.evse_id,
                        tx.meter_current - tx.meter_start
                    );
                    evse.transaction = None;
                    evse.status = ConnectorStatus::Available;
                    return GenericStatus::Accepted;
                }
            }
        }

        warn!("Transaction {} not found", transaction_id);
        GenericStatus::Rejected
    }

    /// Handle ReserveNow
    pub fn reserve(
        &mut self,
        reservation_id: i32,
        evse_id: Option<i32>,
        id_token: String,
        expiry: DateTime<Utc>,
    ) -> ReservationStatus {
        let evse_id = evse_id.or_else(|| {
            self.evses.iter()
                .find(|(_, e)| e.status == ConnectorStatus::Available && e.reservation.is_none())
                .map(|(id, _)| *id)
        });

        if let Some(evse_id) = evse_id {
            if let Some(evse) = self.evses.get_mut(&evse_id) {
                if evse.status != ConnectorStatus::Available {
                    return ReservationStatus::Occupied;
                }
                if evse.reservation.is_some() {
                    return ReservationStatus::Occupied;
                }

                evse.reservation = Some(ActiveReservation {
                    reservation_id,
                    id_token: id_token.clone(),
                    expiry,
                });
                evse.status = ConnectorStatus::Reserved;

                info!(
                    "Reserved EVSE {} for token {}, expires {}",
                    evse_id, id_token, expiry
                );

                return ReservationStatus::Accepted;
            }
        }

        ReservationStatus::Rejected
    }

    /// Handle CancelReservation
    pub fn cancel_reservation(&mut self, reservation_id: i32) -> GenericStatus {
        for evse in self.evses.values_mut() {
            if let Some(ref res) = evse.reservation {
                if res.reservation_id == reservation_id {
                    info!("Cancelled reservation {} on EVSE {}", reservation_id, evse.evse_id);
                    evse.reservation = None;
                    evse.status = ConnectorStatus::Available;
                    return GenericStatus::Accepted;
                }
            }
        }

        warn!("Reservation {} not found", reservation_id);
        GenericStatus::Rejected
    }

    /// Get current status of all EVSEs
    pub fn evse_statuses(&self) -> Vec<(i32, i32, ConnectorStatus)> {
        self.evses
            .values()
            .map(|e| (e.evse_id, e.connector_id, e.status))
            .collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_session_lifecycle() {
        let mut session = Session::new("CS001", "Elektrokombinacija", "EK3-OCPP");
        session.add_evse(1, 1);

        assert_eq!(session.state, SessionState::Disconnected);

        session.handle_event(SessionEvent::Connected);
        assert_eq!(session.state, SessionState::Connected);

        session.handle_event(SessionEvent::BootAccepted { interval: 60 });
        assert_eq!(session.state, SessionState::Registered);
        assert_eq!(session.heartbeat_interval, 60);
    }

    #[test]
    fn test_transaction_lifecycle() {
        let mut session = Session::new("CS001", "Elektrokombinacija", "EK3-OCPP");
        session.add_evse(1, 1);

        let (status, tx_id) = session.start_transaction(Some(1), "TOKEN123".into(), 1);
        assert_eq!(status, GenericStatus::Accepted);
        assert!(tx_id.is_some());

        let tx_id = tx_id.unwrap();
        assert_eq!(session.evses[&1].status, ConnectorStatus::Occupied);

        let status = session.stop_transaction(&tx_id);
        assert_eq!(status, GenericStatus::Accepted);
        assert_eq!(session.evses[&1].status, ConnectorStatus::Available);
    }

    #[test]
    fn test_charging_profile() {
        let mut session = Session::new("CS001", "Elektrokombinacija", "EK3-OCPP");
        session.add_evse(1, 1);

        let profile = ChargingProfile {
            id: 1,
            stack_level: 0,
            charging_profile_purpose: ChargingProfilePurpose::TxDefaultProfile,
            charging_profile_kind: ChargingProfileKind::Absolute,
            charging_schedule: vec![ChargingSchedule {
                id: 1,
                charging_rate_unit: ChargingRateUnit::W,
                charging_schedule_period: vec![
                    ChargingSchedulePeriod {
                        start_period: 0,
                        limit: 15000.0,
                        number_phases: Some(3),
                        phase_to_use: None,
                    },
                ],
                start_schedule: None,
                duration: None,
                min_charging_rate: None,
            }],
            valid_from: None,
            valid_to: None,
            recurrency_kind: None,
            transaction_id: None,
        };

        let status = session.set_charging_profile(1, profile);
        assert_eq!(status, GenericStatus::Accepted);

        let limit = session.evses[&1].active_power_limit_kw();
        assert_eq!(limit, Some(15.0)); // 15000 W = 15 kW
    }
}
