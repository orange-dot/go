//! ROJ state bridge for OCPP queries
//!
//! Provides read access to the ROJ consensus state for responding to
//! OCPP queries like MeterValues and StatusNotification.

use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::RwLock;
use serde_json::Value;
use tracing::debug;

use crate::ocpp::types::*;
use super::translator::{keys, PowerLimitValue, SessionValue, ReservationValue};

/// Bridge between ROJ consensus state and OCPP queries
pub struct StateBridge {
    station_id: String,
    /// Reference to ROJ committed state
    state: Arc<RwLock<HashMap<String, Value>>>,
    /// Simulated meter values (in real implementation, from hardware)
    meters: Arc<RwLock<HashMap<i32, MeterState>>>,
}

impl StateBridge {
    /// Create a new state bridge
    pub fn new(
        station_id: impl Into<String>,
        state: Arc<RwLock<HashMap<String, Value>>>,
    ) -> Self {
        Self {
            station_id: station_id.into(),
            state,
            meters: Arc::new(RwLock::new(HashMap::new())),
        }
    }

    /// Get the power limit for an EVSE
    pub async fn get_power_limit(&self, evse_id: i32) -> Option<PowerLimitValue> {
        let key = format!("{}:{}:{}", keys::POWER_LIMIT, self.station_id, evse_id);
        let state = self.state.read().await;

        state.get(&key)
            .and_then(|v| serde_json::from_value(v.clone()).ok())
    }

    /// Get the session state for an EVSE
    pub async fn get_session(&self, evse_id: i32) -> Option<SessionValue> {
        let key = format!("{}:{}:{}", keys::SESSION, self.station_id, evse_id);
        let state = self.state.read().await;

        state.get(&key)
            .and_then(|v| serde_json::from_value(v.clone()).ok())
    }

    /// Get the reservation for an EVSE
    pub async fn get_reservation(&self, evse_id: i32) -> Option<ReservationValue> {
        let key = format!("{}:{}:{}", keys::RESERVATION, self.station_id, evse_id);
        let state = self.state.read().await;

        state.get(&key)
            .and_then(|v| {
                if v.is_null() {
                    None
                } else {
                    serde_json::from_value(v.clone()).ok()
                }
            })
    }

    /// Determine connector status from ROJ state
    pub async fn get_connector_status(&self, evse_id: i32) -> ConnectorStatus {
        let session = self.get_session(evse_id).await;
        let reservation = self.get_reservation(evse_id).await;
        let power_limit = self.get_power_limit(evse_id).await;

        // Session takes priority
        if let Some(ref s) = session {
            if s.active {
                return ConnectorStatus::Occupied;
            }
        }

        // Then reservation
        if reservation.is_some() {
            return ConnectorStatus::Reserved;
        }

        // Check if explicitly unavailable (power limit = 0)
        if let Some(ref p) = power_limit {
            if p.limit_kw == 0.0 {
                return ConnectorStatus::Unavailable;
            }
        }

        ConnectorStatus::Available
    }

    /// Get full EVSE state for OCPP reporting
    pub async fn get_evse_state(&self, evse_id: i32, connector_id: i32) -> EvseState {
        let status = self.get_connector_status(evse_id).await;
        let session = self.get_session(evse_id).await;
        let reservation = self.get_reservation(evse_id).await;
        let power_limit = self.get_power_limit(evse_id).await;

        let meters = self.meters.read().await;
        let meter_values = meters.get(&evse_id).cloned();

        EvseState {
            evse_id,
            connector_id,
            status,
            active_session: session.and_then(|s| {
                if s.active {
                    Some(SessionState {
                        transaction_id: s.transaction_id.unwrap_or_default(),
                        id_token: s.id_token,
                        started_at: s.started_at.unwrap_or_else(chrono::Utc::now),
                    })
                } else {
                    None
                }
            }),
            active_reservation: reservation.map(|r| ReservationState {
                reservation_id: r.reservation_id,
                id_token: r.id_token,
                expiry: r.expiry,
            }),
            active_profile: power_limit.map(|p| ChargingProfileState {
                profile_id: p.profile_id.unwrap_or(0),
                stack_level: p.stack_level,
                limit_kw: p.limit_kw,
            }),
            meter_values,
        }
    }

    /// Update meter values (from simulated or real hardware)
    pub async fn update_meter(&self, evse_id: i32, power_kw: f64, energy_kwh: f64, soc: Option<f64>) {
        let mut meters = self.meters.write().await;
        meters.insert(
            evse_id,
            MeterState {
                power_kw,
                energy_kwh,
                soc_percent: soc,
                timestamp: chrono::Utc::now(),
            },
        );
    }

    /// Get current meter values for an EVSE
    pub async fn get_meter(&self, evse_id: i32) -> Option<MeterState> {
        let meters = self.meters.read().await;
        meters.get(&evse_id).cloned()
    }

    /// Create MeterValue message for OCPP
    pub async fn create_meter_values(&self, evse_id: i32) -> Option<Vec<MeterValue>> {
        let meter = self.get_meter(evse_id).await?;

        let mut sampled_values = vec![
            SampledValue {
                value: meter.power_kw * 1000.0, // Convert to W
                context: Some(ReadingContext::SamplePeriodic),
                measurand: Some(Measurand::PowerActiveImport),
                phase: None,
                unit_of_measure: Some(UnitOfMeasure::W),
            },
            SampledValue {
                value: meter.energy_kwh * 1000.0, // Convert to Wh
                context: Some(ReadingContext::SamplePeriodic),
                measurand: Some(Measurand::EnergyActiveImportRegister),
                phase: None,
                unit_of_measure: Some(UnitOfMeasure::Wh),
            },
        ];

        if let Some(soc) = meter.soc_percent {
            sampled_values.push(SampledValue {
                value: soc,
                context: Some(ReadingContext::SamplePeriodic),
                measurand: Some(Measurand::SoC),
                phase: None,
                unit_of_measure: Some(UnitOfMeasure::Percent),
            });
        }

        Some(vec![MeterValue {
            timestamp: meter.timestamp,
            sampled_value: sampled_values,
        }])
    }

    /// Get all keys matching a prefix
    pub async fn get_keys_with_prefix(&self, prefix: &str) -> Vec<String> {
        let state = self.state.read().await;
        state
            .keys()
            .filter(|k| k.starts_with(prefix))
            .cloned()
            .collect()
    }

    /// Find EVSE ID by transaction ID
    pub async fn find_evse_by_transaction(&self, transaction_id: &str) -> Option<i32> {
        let prefix = format!("{}:{}", keys::SESSION, self.station_id);
        let keys = self.get_keys_with_prefix(&prefix).await;

        for key in keys {
            if let Some(session) = self.get_session_from_key(&key).await {
                if session.transaction_id.as_deref() == Some(transaction_id) {
                    // Parse EVSE ID from key
                    let parts: Vec<&str> = key.split(':').collect();
                    if parts.len() == 3 {
                        return parts[2].parse().ok();
                    }
                }
            }
        }

        None
    }

    /// Find EVSE ID by reservation ID
    pub async fn find_evse_by_reservation(&self, reservation_id: i32) -> Option<i32> {
        let prefix = format!("{}:{}", keys::RESERVATION, self.station_id);
        let keys = self.get_keys_with_prefix(&prefix).await;

        for key in keys {
            if let Some(reservation) = self.get_reservation_from_key(&key).await {
                if reservation.reservation_id == reservation_id {
                    // Parse EVSE ID from key
                    let parts: Vec<&str> = key.split(':').collect();
                    if parts.len() == 3 {
                        return parts[2].parse().ok();
                    }
                }
            }
        }

        None
    }

    /// Get session from full key
    async fn get_session_from_key(&self, key: &str) -> Option<SessionValue> {
        let state = self.state.read().await;
        state.get(key)
            .and_then(|v| serde_json::from_value(v.clone()).ok())
    }

    /// Get reservation from full key
    async fn get_reservation_from_key(&self, key: &str) -> Option<ReservationValue> {
        let state = self.state.read().await;
        state.get(key)
            .and_then(|v| {
                if v.is_null() {
                    None
                } else {
                    serde_json::from_value(v.clone()).ok()
                }
            })
    }

    /// Debug: dump current state
    pub async fn dump_state(&self) -> HashMap<String, Value> {
        self.state.read().await.clone()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Arc;
    use tokio::sync::RwLock;

    #[tokio::test]
    async fn test_state_bridge_power_limit() {
        let state = Arc::new(RwLock::new(HashMap::new()));
        let bridge = StateBridge::new("CS001", state.clone());

        // Initially no power limit
        assert!(bridge.get_power_limit(1).await.is_none());

        // Add power limit
        {
            let mut s = state.write().await;
            s.insert(
                "power_limit:CS001:1".to_string(),
                serde_json::to_value(PowerLimitValue {
                    limit_kw: 22.0,
                    stack_level: 0,
                    profile_id: Some(1),
                    valid_until: None,
                })
                .unwrap(),
            );
        }

        let power = bridge.get_power_limit(1).await.unwrap();
        assert_eq!(power.limit_kw, 22.0);
    }

    #[tokio::test]
    async fn test_state_bridge_status() {
        let state = Arc::new(RwLock::new(HashMap::new()));
        let bridge = StateBridge::new("CS001", state.clone());

        // Initially available
        assert_eq!(
            bridge.get_connector_status(1).await,
            ConnectorStatus::Available
        );

        // Add active session
        {
            let mut s = state.write().await;
            s.insert(
                "session:CS001:1".to_string(),
                serde_json::to_value(SessionValue {
                    active: true,
                    id_token: "TOKEN123".to_string(),
                    transaction_id: Some("tx-001".to_string()),
                    started_at: Some(chrono::Utc::now()),
                })
                .unwrap(),
            );
        }

        assert_eq!(
            bridge.get_connector_status(1).await,
            ConnectorStatus::Occupied
        );
    }

    #[tokio::test]
    async fn test_meter_values() {
        let state = Arc::new(RwLock::new(HashMap::new()));
        let bridge = StateBridge::new("CS001", state);

        // Update meter
        bridge.update_meter(1, 11.0, 5.5, Some(75.0)).await;

        // Get meter values
        let meters = bridge.create_meter_values(1).await.unwrap();
        assert_eq!(meters.len(), 1);
        assert_eq!(meters[0].sampled_value.len(), 3);
    }
}
