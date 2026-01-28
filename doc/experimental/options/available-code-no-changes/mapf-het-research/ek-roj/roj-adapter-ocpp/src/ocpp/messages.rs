//! OCPP 2.0.1 JSON-RPC message framing
//!
//! OCPP uses JSON-RPC 2.0 over WebSocket with a specific message format:
//! - CALL: [2, messageId, action, payload]
//! - CALLRESULT: [3, messageId, payload]
//! - CALLERROR: [4, messageId, errorCode, errorDescription, errorDetails]

use serde::{Deserialize, Serialize};
use serde_json::Value;
use thiserror::Error;
use uuid::Uuid;

use super::types::*;

/// OCPP message type identifiers
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MessageType {
    Call = 2,
    CallResult = 3,
    CallError = 4,
}

/// OCPP error codes
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum ErrorCode {
    FormatViolation,
    GenericError,
    InternalError,
    MessageTypeNotSupported,
    NotImplemented,
    NotSupported,
    OccurrenceConstraintViolation,
    PropertyConstraintViolation,
    ProtocolError,
    RpcFrameworkError,
    SecurityError,
    TypeConstraintViolation,
}

/// OCPP action names
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum Action {
    // CP -> CSMS
    BootNotification,
    Heartbeat,
    StatusNotification,
    MeterValues,
    TransactionEvent,

    // CSMS -> CP
    SetChargingProfile,
    RequestStartTransaction,
    RequestStopTransaction,
    ReserveNow,
    CancelReservation,
    GetVariables,
    SetVariables,
    Reset,

    // Bidirectional
    DataTransfer,
}

impl std::fmt::Display for Action {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self)
    }
}

impl std::str::FromStr for Action {
    type Err = OcppError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "BootNotification" => Ok(Action::BootNotification),
            "Heartbeat" => Ok(Action::Heartbeat),
            "StatusNotification" => Ok(Action::StatusNotification),
            "MeterValues" => Ok(Action::MeterValues),
            "TransactionEvent" => Ok(Action::TransactionEvent),
            "SetChargingProfile" => Ok(Action::SetChargingProfile),
            "RequestStartTransaction" => Ok(Action::RequestStartTransaction),
            "RequestStopTransaction" => Ok(Action::RequestStopTransaction),
            "ReserveNow" => Ok(Action::ReserveNow),
            "CancelReservation" => Ok(Action::CancelReservation),
            "GetVariables" => Ok(Action::GetVariables),
            "SetVariables" => Ok(Action::SetVariables),
            "Reset" => Ok(Action::Reset),
            "DataTransfer" => Ok(Action::DataTransfer),
            _ => Err(OcppError::UnknownAction(s.to_string())),
        }
    }
}

/// Errors in OCPP message handling
#[derive(Debug, Error)]
pub enum OcppError {
    #[error("JSON parse error: {0}")]
    JsonError(#[from] serde_json::Error),

    #[error("Invalid message format")]
    InvalidFormat,

    #[error("Unknown action: {0}")]
    UnknownAction(String),

    #[error("Unknown message type: {0}")]
    UnknownMessageType(i64),

    #[error("OCPP error from CSMS: {code:?} - {description}")]
    RemoteError {
        code: ErrorCode,
        description: String,
        details: Value,
    },

    #[error("Timeout waiting for response")]
    Timeout,

    #[error("Connection closed")]
    ConnectionClosed,
}

/// OCPP CALL message (request)
#[derive(Debug, Clone)]
pub struct Call {
    pub message_id: String,
    pub action: Action,
    pub payload: Value,
}

impl Call {
    /// Create a new CALL message with auto-generated ID
    pub fn new(action: Action, payload: impl Serialize) -> Result<Self, OcppError> {
        Ok(Self {
            message_id: Uuid::new_v4().to_string(),
            action,
            payload: serde_json::to_value(payload)?,
        })
    }

    /// Create BootNotification call
    pub fn boot_notification(station: ChargingStationInfo, reason: BootReason) -> Result<Self, OcppError> {
        Self::new(
            Action::BootNotification,
            BootNotificationRequest {
                charging_station: station,
                reason,
            },
        )
    }

    /// Create Heartbeat call
    pub fn heartbeat() -> Result<Self, OcppError> {
        Self::new(Action::Heartbeat, HeartbeatRequest {})
    }

    /// Create StatusNotification call
    pub fn status_notification(
        evse_id: i32,
        connector_id: i32,
        status: ConnectorStatus,
    ) -> Result<Self, OcppError> {
        Self::new(
            Action::StatusNotification,
            StatusNotificationRequest {
                timestamp: chrono::Utc::now(),
                connector_status: status,
                evse_id,
                connector_id,
            },
        )
    }

    /// Create MeterValues call
    pub fn meter_values(evse_id: i32, meter_value: Vec<MeterValue>) -> Result<Self, OcppError> {
        Self::new(
            Action::MeterValues,
            MeterValuesRequest { evse_id, meter_value },
        )
    }

    /// Serialize to OCPP wire format: [2, messageId, action, payload]
    pub fn to_bytes(&self) -> Result<Vec<u8>, OcppError> {
        let array = serde_json::json!([
            MessageType::Call as i32,
            &self.message_id,
            self.action.to_string(),
            &self.payload
        ]);
        Ok(serde_json::to_vec(&array)?)
    }
}

/// OCPP CALLRESULT message (success response)
#[derive(Debug, Clone)]
pub struct CallResult {
    pub message_id: String,
    pub payload: Value,
}

impl CallResult {
    /// Create a new CALLRESULT message
    pub fn new(message_id: String, payload: impl Serialize) -> Result<Self, OcppError> {
        Ok(Self {
            message_id,
            payload: serde_json::to_value(payload)?,
        })
    }

    /// Create response for SetChargingProfile
    pub fn set_charging_profile(message_id: String, status: GenericStatus) -> Result<Self, OcppError> {
        Self::new(
            message_id,
            SetChargingProfileResponse {
                status,
                status_info: None,
            },
        )
    }

    /// Create response for RequestStartTransaction
    pub fn request_start_transaction(
        message_id: String,
        status: GenericStatus,
        transaction_id: Option<String>,
    ) -> Result<Self, OcppError> {
        Self::new(
            message_id,
            RequestStartTransactionResponse {
                status,
                transaction_id,
                status_info: None,
            },
        )
    }

    /// Create response for RequestStopTransaction
    pub fn request_stop_transaction(message_id: String, status: GenericStatus) -> Result<Self, OcppError> {
        Self::new(
            message_id,
            RequestStopTransactionResponse {
                status,
                status_info: None,
            },
        )
    }

    /// Create response for ReserveNow
    pub fn reserve_now(message_id: String, status: ReservationStatus) -> Result<Self, OcppError> {
        Self::new(
            message_id,
            ReserveNowResponse {
                status,
                status_info: None,
            },
        )
    }

    /// Create response for CancelReservation
    pub fn cancel_reservation(message_id: String, status: GenericStatus) -> Result<Self, OcppError> {
        Self::new(
            message_id,
            CancelReservationResponse {
                status,
                status_info: None,
            },
        )
    }

    /// Serialize to OCPP wire format: [3, messageId, payload]
    pub fn to_bytes(&self) -> Result<Vec<u8>, OcppError> {
        let array = serde_json::json!([
            MessageType::CallResult as i32,
            &self.message_id,
            &self.payload
        ]);
        Ok(serde_json::to_vec(&array)?)
    }

    /// Parse the payload as a specific response type
    pub fn parse_payload<T: for<'de> Deserialize<'de>>(&self) -> Result<T, OcppError> {
        Ok(serde_json::from_value(self.payload.clone())?)
    }
}

/// OCPP CALLERROR message (error response)
#[derive(Debug, Clone)]
pub struct CallError {
    pub message_id: String,
    pub error_code: ErrorCode,
    pub error_description: String,
    pub error_details: Value,
}

impl CallError {
    /// Create a new CALLERROR message
    pub fn new(
        message_id: String,
        error_code: ErrorCode,
        error_description: impl Into<String>,
    ) -> Self {
        Self {
            message_id,
            error_code,
            error_description: error_description.into(),
            error_details: Value::Object(serde_json::Map::new()),
        }
    }

    /// Serialize to OCPP wire format: [4, messageId, errorCode, errorDescription, errorDetails]
    pub fn to_bytes(&self) -> Result<Vec<u8>, OcppError> {
        let array = serde_json::json!([
            MessageType::CallError as i32,
            &self.message_id,
            format!("{:?}", self.error_code),
            &self.error_description,
            &self.error_details
        ]);
        Ok(serde_json::to_vec(&array)?)
    }
}

/// Parsed OCPP message (any type)
#[derive(Debug, Clone)]
pub enum OcppMessage {
    Call(Call),
    CallResult(CallResult),
    CallError(CallError),
}

impl OcppMessage {
    /// Parse an OCPP message from JSON bytes
    pub fn parse(bytes: &[u8]) -> Result<Self, OcppError> {
        let array: Vec<Value> = serde_json::from_slice(bytes)?;

        if array.is_empty() {
            return Err(OcppError::InvalidFormat);
        }

        let msg_type = array[0]
            .as_i64()
            .ok_or(OcppError::InvalidFormat)?;

        match msg_type {
            2 => {
                // CALL: [2, messageId, action, payload]
                if array.len() != 4 {
                    return Err(OcppError::InvalidFormat);
                }

                let message_id = array[1]
                    .as_str()
                    .ok_or(OcppError::InvalidFormat)?
                    .to_string();

                let action_str = array[2]
                    .as_str()
                    .ok_or(OcppError::InvalidFormat)?;

                let action: Action = action_str.parse()?;
                let payload = array[3].clone();

                Ok(OcppMessage::Call(Call {
                    message_id,
                    action,
                    payload,
                }))
            }
            3 => {
                // CALLRESULT: [3, messageId, payload]
                if array.len() != 3 {
                    return Err(OcppError::InvalidFormat);
                }

                let message_id = array[1]
                    .as_str()
                    .ok_or(OcppError::InvalidFormat)?
                    .to_string();

                let payload = array[2].clone();

                Ok(OcppMessage::CallResult(CallResult {
                    message_id,
                    payload,
                }))
            }
            4 => {
                // CALLERROR: [4, messageId, errorCode, errorDescription, errorDetails]
                if array.len() != 5 {
                    return Err(OcppError::InvalidFormat);
                }

                let message_id = array[1]
                    .as_str()
                    .ok_or(OcppError::InvalidFormat)?
                    .to_string();

                let error_code_str = array[2]
                    .as_str()
                    .ok_or(OcppError::InvalidFormat)?;

                let error_code: ErrorCode = serde_json::from_value(
                    Value::String(error_code_str.to_string())
                ).unwrap_or(ErrorCode::GenericError);

                let error_description = array[3]
                    .as_str()
                    .unwrap_or("")
                    .to_string();

                let error_details = array[4].clone();

                Ok(OcppMessage::CallError(CallError {
                    message_id,
                    error_code,
                    error_description,
                    error_details,
                }))
            }
            _ => Err(OcppError::UnknownMessageType(msg_type)),
        }
    }

    /// Get the message ID
    pub fn message_id(&self) -> &str {
        match self {
            OcppMessage::Call(c) => &c.message_id,
            OcppMessage::CallResult(r) => &r.message_id,
            OcppMessage::CallError(e) => &e.message_id,
        }
    }

    /// Serialize to bytes
    pub fn to_bytes(&self) -> Result<Vec<u8>, OcppError> {
        match self {
            OcppMessage::Call(c) => c.to_bytes(),
            OcppMessage::CallResult(r) => r.to_bytes(),
            OcppMessage::CallError(e) => e.to_bytes(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_call_serialization() {
        let call = Call::heartbeat().unwrap();
        let bytes = call.to_bytes().unwrap();
        let text = String::from_utf8(bytes).unwrap();

        assert!(text.starts_with("[2,"));
        assert!(text.contains("\"Heartbeat\""));
    }

    #[test]
    fn test_call_parsing() {
        let json = r#"[2, "msg-123", "Heartbeat", {}]"#;
        let msg = OcppMessage::parse(json.as_bytes()).unwrap();

        match msg {
            OcppMessage::Call(call) => {
                assert_eq!(call.message_id, "msg-123");
                assert_eq!(call.action, Action::Heartbeat);
            }
            _ => panic!("Expected Call"),
        }
    }

    #[test]
    fn test_call_result_parsing() {
        let json = r#"[3, "msg-123", {"currentTime": "2026-01-20T12:00:00Z"}]"#;
        let msg = OcppMessage::parse(json.as_bytes()).unwrap();

        match msg {
            OcppMessage::CallResult(result) => {
                assert_eq!(result.message_id, "msg-123");
            }
            _ => panic!("Expected CallResult"),
        }
    }

    #[test]
    fn test_call_error_parsing() {
        let json = r#"[4, "msg-123", "NotImplemented", "Action not supported", {}]"#;
        let msg = OcppMessage::parse(json.as_bytes()).unwrap();

        match msg {
            OcppMessage::CallError(error) => {
                assert_eq!(error.message_id, "msg-123");
                assert_eq!(error.error_code, ErrorCode::NotImplemented);
            }
            _ => panic!("Expected CallError"),
        }
    }

    #[test]
    fn test_set_charging_profile_request() {
        let json = r#"[2, "uuid-456", "SetChargingProfile", {
            "evseId": 1,
            "chargingProfile": {
                "id": 1,
                "stackLevel": 0,
                "chargingProfilePurpose": "TxDefaultProfile",
                "chargingProfileKind": "Absolute",
                "chargingSchedule": [{
                    "id": 1,
                    "chargingRateUnit": "W",
                    "chargingSchedulePeriod": [
                        {"startPeriod": 0, "limit": 22000.0}
                    ]
                }]
            }
        }]"#;

        let msg = OcppMessage::parse(json.as_bytes()).unwrap();

        match msg {
            OcppMessage::Call(call) => {
                assert_eq!(call.action, Action::SetChargingProfile);
                let req: SetChargingProfileRequest =
                    serde_json::from_value(call.payload).unwrap();
                assert_eq!(req.evse_id, 1);
                assert_eq!(req.charging_profile.id, 1);
            }
            _ => panic!("Expected Call"),
        }
    }
}
