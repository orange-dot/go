//! OCPP <-> ROJ message translation
//!
//! Translates between OCPP 2.0.1 messages and ROJ consensus proposals.
//!
//! ## OCPP -> ROJ Mapping
//!
//! | OCPP Action              | ROJ Key                  | ROJ Value                      | Consensus? |
//! |--------------------------|--------------------------|--------------------------------|------------|
//! | SetChargingProfile       | `power_limit:{connector}`| `{limit_kw, stack_level}`      | Yes        |
//! | RequestStartTransaction  | `session:{connector}`    | `{active:1, id_token}`         | Yes        |
//! | RequestStopTransaction   | `session:{connector}`    | `{active:0}`                   | Yes        |
//! | ReserveNow               | `reservation:{connector}`| `{expiry, id_token}`           | Yes        |
//! | CancelReservation        | `reservation:{connector}`| null                           | Yes        |
//! | BootNotification         | (ANNOUNCE only)          | -                              | No         |
//!
//! ## ROJ -> OCPP Mapping
//!
//! ROJ COMMIT events trigger OCPP StatusNotification to inform CSMS of state changes.

use chrono::{DateTime, Utc};
use roj_core::{Message as RojMessage, unix_timestamp};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use tracing::{debug, info, warn};

use crate::ocpp::types::*;
use crate::ocpp::messages::*;

/// ROJ key prefixes for different OCPP resources
pub mod keys {
    pub const POWER_LIMIT: &str = "power_limit";
    pub const SESSION: &str = "session";
    pub const RESERVATION: &str = "reservation";
    pub const STATION: &str = "station";
}

/// Power limit value for ROJ consensus
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PowerLimitValue {
    pub limit_kw: f64,
    pub stack_level: i32,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub profile_id: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub valid_until: Option<DateTime<Utc>>,
}

/// Session state value for ROJ consensus
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SessionValue {
    pub active: bool,
    pub id_token: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub transaction_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub started_at: Option<DateTime<Utc>>,
}

/// Reservation value for ROJ consensus
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ReservationValue {
    pub reservation_id: i32,
    pub id_token: String,
    pub expiry: DateTime<Utc>,
}

/// Station info value for ROJ (via ANNOUNCE)
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StationValue {
    pub vendor: String,
    pub model: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub serial_number: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub firmware_version: Option<String>,
}

/// Translate OCPP messages to ROJ proposals
pub struct OcppToRoj {
    node_id: String,
    station_id: String,
}

impl OcppToRoj {
    /// Create a new OCPP to ROJ translator
    pub fn new(node_id: impl Into<String>, station_id: impl Into<String>) -> Self {
        Self {
            node_id: node_id.into(),
            station_id: station_id.into(),
        }
    }

    /// Generate ROJ key for power limit
    fn power_limit_key(&self, evse_id: i32) -> String {
        format!("{}:{}:{}", keys::POWER_LIMIT, self.station_id, evse_id)
    }

    /// Generate ROJ key for session
    fn session_key(&self, evse_id: i32) -> String {
        format!("{}:{}:{}", keys::SESSION, self.station_id, evse_id)
    }

    /// Generate ROJ key for reservation
    fn reservation_key(&self, evse_id: i32) -> String {
        format!("{}:{}:{}", keys::RESERVATION, self.station_id, evse_id)
    }

    /// Translate SetChargingProfile to ROJ PROPOSE
    pub fn translate_set_charging_profile(
        &self,
        req: &SetChargingProfileRequest,
    ) -> Option<(String, Value)> {
        // Extract the current power limit from the profile
        let schedule = req.charging_profile.charging_schedule.first()?;
        let period = schedule.charging_schedule_period.first()?;

        // Convert to kW
        let limit_kw = match schedule.charging_rate_unit {
            ChargingRateUnit::W => period.limit / 1000.0,
            ChargingRateUnit::A => {
                // Assume 3-phase 230V for Amp -> kW conversion
                let phases = period.number_phases.unwrap_or(3) as f64;
                period.limit * 230.0 * phases / 1000.0
            }
        };

        let value = PowerLimitValue {
            limit_kw,
            stack_level: req.charging_profile.stack_level,
            profile_id: Some(req.charging_profile.id),
            valid_until: req.charging_profile.valid_to,
        };

        let key = self.power_limit_key(req.evse_id);
        info!(
            "Translating SetChargingProfile: {} = {} kW (stack {})",
            key, limit_kw, req.charging_profile.stack_level
        );

        Some((key, serde_json::to_value(value).ok()?))
    }

    /// Translate RequestStartTransaction to ROJ PROPOSE
    pub fn translate_start_transaction(
        &self,
        req: &RequestStartTransactionRequest,
    ) -> Option<(String, Value)> {
        let evse_id = req.evse_id.unwrap_or(1);
        let transaction_id = format!("tx-{}", uuid::Uuid::new_v4().to_string()[..8].to_string());

        let value = SessionValue {
            active: true,
            id_token: req.id_token.id_token.clone(),
            transaction_id: Some(transaction_id.clone()),
            started_at: Some(Utc::now()),
        };

        let key = self.session_key(evse_id);
        info!(
            "Translating RequestStartTransaction: {} = active, token={}",
            key, req.id_token.id_token
        );

        Some((key, serde_json::to_value(value).ok()?))
    }

    /// Translate RequestStopTransaction to ROJ PROPOSE
    pub fn translate_stop_transaction(
        &self,
        req: &RequestStopTransactionRequest,
        evse_id: i32,
    ) -> Option<(String, Value)> {
        let value = SessionValue {
            active: false,
            id_token: String::new(),
            transaction_id: Some(req.transaction_id.clone()),
            started_at: None,
        };

        let key = self.session_key(evse_id);
        info!(
            "Translating RequestStopTransaction: {} = inactive, tx={}",
            key, req.transaction_id
        );

        Some((key, serde_json::to_value(value).ok()?))
    }

    /// Translate ReserveNow to ROJ PROPOSE
    pub fn translate_reserve_now(&self, req: &ReserveNowRequest) -> Option<(String, Value)> {
        let evse_id = req.evse_id.unwrap_or(1);

        let value = ReservationValue {
            reservation_id: req.id,
            id_token: req.id_token.id_token.clone(),
            expiry: req.expiry_date_time,
        };

        let key = self.reservation_key(evse_id);
        info!(
            "Translating ReserveNow: {} = res {}, token={}, expires={}",
            key, req.id, req.id_token.id_token, req.expiry_date_time
        );

        Some((key, serde_json::to_value(value).ok()?))
    }

    /// Translate CancelReservation to ROJ PROPOSE (null value to remove)
    pub fn translate_cancel_reservation(
        &self,
        req: &CancelReservationRequest,
        evse_id: i32,
    ) -> Option<(String, Value)> {
        let key = self.reservation_key(evse_id);
        info!(
            "Translating CancelReservation: {} = null (reservation {})",
            key, req.reservation_id
        );

        Some((key, Value::Null))
    }

    /// Create a ROJ PROPOSE message from the translated OCPP request
    pub fn create_proposal(&self, key: String, value: Value) -> RojMessage {
        let proposal_id = uuid::Uuid::new_v4().to_string()[..8].to_string();
        let timestamp = unix_timestamp();

        debug!(
            "Creating ROJ PROPOSE: id={}, key={}, value={}",
            proposal_id, key, value
        );

        RojMessage::Propose {
            proposal_id,
            from: self.node_id.clone(),
            key,
            value,
            timestamp,
        }
    }

    /// Translate a full OCPP CALL to ROJ PROPOSE (if applicable)
    pub fn translate_call(&self, call: &Call) -> Option<RojMessage> {
        let (key, value) = match &call.action {
            Action::SetChargingProfile => {
                let req: SetChargingProfileRequest =
                    serde_json::from_value(call.payload.clone()).ok()?;
                self.translate_set_charging_profile(&req)?
            }
            Action::RequestStartTransaction => {
                let req: RequestStartTransactionRequest =
                    serde_json::from_value(call.payload.clone()).ok()?;
                self.translate_start_transaction(&req)?
            }
            Action::RequestStopTransaction => {
                let req: RequestStopTransactionRequest =
                    serde_json::from_value(call.payload.clone()).ok()?;
                // Need to find EVSE from transaction ID - use default 1
                self.translate_stop_transaction(&req, 1)?
            }
            Action::ReserveNow => {
                let req: ReserveNowRequest =
                    serde_json::from_value(call.payload.clone()).ok()?;
                self.translate_reserve_now(&req)?
            }
            Action::CancelReservation => {
                let req: CancelReservationRequest =
                    serde_json::from_value(call.payload.clone()).ok()?;
                // Need to find EVSE from reservation ID - use default 1
                self.translate_cancel_reservation(&req, 1)?
            }
            _ => {
                debug!("Action {:?} does not require ROJ consensus", call.action);
                return None;
            }
        };

        Some(self.create_proposal(key, value))
    }
}

/// Translate ROJ events back to OCPP notifications
pub struct RojToOcpp {
    station_id: String,
}

impl RojToOcpp {
    /// Create a new ROJ to OCPP translator
    pub fn new(station_id: impl Into<String>) -> Self {
        Self {
            station_id: station_id.into(),
        }
    }

    /// Parse ROJ key to extract type and EVSE ID
    fn parse_key<'a>(&self, key: &'a str) -> Option<(&'a str, i32)> {
        let parts: Vec<&str> = key.split(':').collect();
        if parts.len() != 3 {
            return None;
        }

        // Verify station ID matches
        if parts[1] != self.station_id {
            return None;
        }

        let evse_id: i32 = parts[2].parse().ok()?;
        Some((parts[0], evse_id))
    }

    /// Handle a ROJ COMMIT event
    pub fn handle_commit(
        &self,
        key: &str,
        value: &Value,
    ) -> Option<CommitEffect> {
        let (key_type, evse_id) = self.parse_key(key)?;

        match key_type {
            keys::POWER_LIMIT => {
                let power: PowerLimitValue = serde_json::from_value(value.clone()).ok()?;
                info!(
                    "ROJ COMMIT power_limit for EVSE {}: {} kW",
                    evse_id, power.limit_kw
                );
                Some(CommitEffect::PowerLimitChanged {
                    evse_id,
                    limit_kw: power.limit_kw,
                    stack_level: power.stack_level,
                })
            }
            keys::SESSION => {
                let session: SessionValue = serde_json::from_value(value.clone()).ok()?;
                info!(
                    "ROJ COMMIT session for EVSE {}: active={}",
                    evse_id, session.active
                );
                Some(CommitEffect::SessionChanged {
                    evse_id,
                    active: session.active,
                    transaction_id: session.transaction_id,
                })
            }
            keys::RESERVATION => {
                if value.is_null() {
                    info!("ROJ COMMIT reservation cancelled for EVSE {}", evse_id);
                    Some(CommitEffect::ReservationCancelled { evse_id })
                } else {
                    let res: ReservationValue = serde_json::from_value(value.clone()).ok()?;
                    info!(
                        "ROJ COMMIT reservation for EVSE {}: id={}",
                        evse_id, res.reservation_id
                    );
                    Some(CommitEffect::ReservationCreated {
                        evse_id,
                        reservation_id: res.reservation_id,
                    })
                }
            }
            _ => {
                debug!("Unknown ROJ key type: {}", key_type);
                None
            }
        }
    }

    /// Determine connector status from ROJ state
    pub fn determine_status(
        &self,
        has_session: bool,
        has_reservation: bool,
        power_limit: Option<f64>,
    ) -> ConnectorStatus {
        if has_session {
            ConnectorStatus::Occupied
        } else if has_reservation {
            ConnectorStatus::Reserved
        } else if power_limit == Some(0.0) {
            ConnectorStatus::Unavailable
        } else {
            ConnectorStatus::Available
        }
    }
}

/// Effect of a ROJ COMMIT on local OCPP state
#[derive(Debug, Clone)]
pub enum CommitEffect {
    PowerLimitChanged {
        evse_id: i32,
        limit_kw: f64,
        stack_level: i32,
    },
    SessionChanged {
        evse_id: i32,
        active: bool,
        transaction_id: Option<String>,
    },
    ReservationCreated {
        evse_id: i32,
        reservation_id: i32,
    },
    ReservationCancelled {
        evse_id: i32,
    },
}

/// Validation result for ROJ proposals
#[derive(Debug, Clone)]
pub enum ProposalValidation {
    /// Accept the proposal
    Accept,
    /// Reject with reason
    Reject(String),
}

/// Validate a ROJ proposal before voting
pub fn validate_proposal(key: &str, value: &Value) -> ProposalValidation {
    let parts: Vec<&str> = key.split(':').collect();
    if parts.len() != 3 {
        return ProposalValidation::Reject("Invalid key format".into());
    }

    match parts[0] {
        keys::POWER_LIMIT => {
            if let Ok(power) = serde_json::from_value::<PowerLimitValue>(value.clone()) {
                if power.limit_kw < 0.0 {
                    return ProposalValidation::Reject("Negative power limit".into());
                }
                if power.limit_kw > 10000.0 {
                    // 10 MW sanity check
                    return ProposalValidation::Reject("Power limit too high".into());
                }
                ProposalValidation::Accept
            } else {
                ProposalValidation::Reject("Invalid power limit format".into())
            }
        }
        keys::SESSION => {
            if serde_json::from_value::<SessionValue>(value.clone()).is_ok() {
                ProposalValidation::Accept
            } else {
                ProposalValidation::Reject("Invalid session format".into())
            }
        }
        keys::RESERVATION => {
            if value.is_null() {
                ProposalValidation::Accept
            } else if serde_json::from_value::<ReservationValue>(value.clone()).is_ok() {
                ProposalValidation::Accept
            } else {
                ProposalValidation::Reject("Invalid reservation format".into())
            }
        }
        _ => ProposalValidation::Accept, // Unknown types are accepted (extensibility)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_translate_set_charging_profile() {
        let translator = OcppToRoj::new("alpha", "CS001");

        let req = SetChargingProfileRequest {
            evse_id: 1,
            charging_profile: ChargingProfile {
                id: 1,
                stack_level: 0,
                charging_profile_purpose: ChargingProfilePurpose::TxDefaultProfile,
                charging_profile_kind: ChargingProfileKind::Absolute,
                charging_schedule: vec![ChargingSchedule {
                    id: 1,
                    charging_rate_unit: ChargingRateUnit::W,
                    charging_schedule_period: vec![ChargingSchedulePeriod {
                        start_period: 0,
                        limit: 22000.0,
                        number_phases: Some(3),
                        phase_to_use: None,
                    }],
                    start_schedule: None,
                    duration: None,
                    min_charging_rate: None,
                }],
                valid_from: None,
                valid_to: None,
                recurrency_kind: None,
                transaction_id: None,
            },
        };

        let (key, value) = translator.translate_set_charging_profile(&req).unwrap();
        assert_eq!(key, "power_limit:CS001:1");

        let power: PowerLimitValue = serde_json::from_value(value).unwrap();
        assert_eq!(power.limit_kw, 22.0);
        assert_eq!(power.stack_level, 0);
    }

    #[test]
    fn test_translate_start_transaction() {
        let translator = OcppToRoj::new("alpha", "CS001");

        let req = RequestStartTransactionRequest {
            id_token: IdToken {
                id_token: "RFID123".to_string(),
                token_type: "ISO14443".to_string(),
            },
            remote_start_id: 1,
            evse_id: Some(1),
            charging_profile: None,
        };

        let (key, value) = translator.translate_start_transaction(&req).unwrap();
        assert_eq!(key, "session:CS001:1");

        let session: SessionValue = serde_json::from_value(value).unwrap();
        assert!(session.active);
        assert_eq!(session.id_token, "RFID123");
    }

    #[test]
    fn test_roj_to_ocpp_commit() {
        let translator = RojToOcpp::new("CS001");

        let value = serde_json::to_value(PowerLimitValue {
            limit_kw: 11.0,
            stack_level: 0,
            profile_id: Some(1),
            valid_until: None,
        })
        .unwrap();

        let effect = translator.handle_commit("power_limit:CS001:1", &value).unwrap();

        match effect {
            CommitEffect::PowerLimitChanged {
                evse_id,
                limit_kw,
                ..
            } => {
                assert_eq!(evse_id, 1);
                assert_eq!(limit_kw, 11.0);
            }
            _ => panic!("Expected PowerLimitChanged"),
        }
    }

    #[test]
    fn test_validate_proposal() {
        // Valid power limit
        let value = serde_json::to_value(PowerLimitValue {
            limit_kw: 22.0,
            stack_level: 0,
            profile_id: None,
            valid_until: None,
        })
        .unwrap();
        assert!(matches!(
            validate_proposal("power_limit:CS001:1", &value),
            ProposalValidation::Accept
        ));

        // Negative power limit
        let value = serde_json::to_value(PowerLimitValue {
            limit_kw: -5.0,
            stack_level: 0,
            profile_id: None,
            valid_until: None,
        })
        .unwrap();
        assert!(matches!(
            validate_proposal("power_limit:CS001:1", &value),
            ProposalValidation::Reject(_)
        ));
    }
}
