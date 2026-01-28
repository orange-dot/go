//! OCPP 2.0.1 message types
//!
//! Implements the core data types from OCPP 2.0.1 specification.
//! Focused on the messages needed for ROJ federation:
//! - SetChargingProfile
//! - RemoteStartTransaction / RemoteStopTransaction
//! - ReserveNow / CancelReservation
//! - StatusNotification
//! - MeterValues
//! - BootNotification / Heartbeat

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};

// ============================================================================
// Enumerations
// ============================================================================

/// Charging station status
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum ConnectorStatus {
    Available,
    Occupied,
    Reserved,
    Unavailable,
    Faulted,
}

/// Charging profile purpose
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum ChargingProfilePurpose {
    ChargingStationMaxProfile,
    TxDefaultProfile,
    TxProfile,
}

/// Charging profile kind
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum ChargingProfileKind {
    Absolute,
    Recurring,
    Relative,
}

/// Charging rate unit
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum ChargingRateUnit {
    W,
    A,
}

/// Recurrency kind for recurring profiles
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum RecurrencyKind {
    Daily,
    Weekly,
}

/// Generic OCPP status for responses
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub enum GenericStatus {
    Accepted,
    Rejected,
}

/// Registration status for BootNotification
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub enum RegistrationStatus {
    Accepted,
    Pending,
    Rejected,
}

/// Authorization status for RemoteStart
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub enum AuthorizationStatus {
    Accepted,
    Blocked,
    Expired,
    Invalid,
    NoCredit,
    NotAllowedTypeEVSE,
    NotAtThisLocation,
    NotAtThisTime,
    Unknown,
}

/// Measurand types for meter values
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum Measurand {
    #[serde(rename = "Current.Import")]
    CurrentImport,
    #[serde(rename = "Current.Export")]
    CurrentExport,
    #[serde(rename = "Energy.Active.Import.Register")]
    EnergyActiveImportRegister,
    #[serde(rename = "Energy.Active.Export.Register")]
    EnergyActiveExportRegister,
    #[serde(rename = "Power.Active.Import")]
    PowerActiveImport,
    #[serde(rename = "Power.Active.Export")]
    PowerActiveExport,
    #[serde(rename = "Voltage")]
    Voltage,
    #[serde(rename = "SoC")]
    SoC,
}

/// Reading context for meter values
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum ReadingContext {
    #[serde(rename = "Interruption.Begin")]
    InterruptionBegin,
    #[serde(rename = "Interruption.End")]
    InterruptionEnd,
    #[serde(rename = "Sample.Clock")]
    SampleClock,
    #[serde(rename = "Sample.Periodic")]
    SamplePeriodic,
    #[serde(rename = "Transaction.Begin")]
    TransactionBegin,
    #[serde(rename = "Transaction.End")]
    TransactionEnd,
    Trigger,
}

/// Unit of measure for sampled values
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum UnitOfMeasure {
    Wh,
    kWh,
    varh,
    kvarh,
    W,
    kW,
    VA,
    kVA,
    var,
    kvar,
    A,
    V,
    Celsius,
    Fahrenheit,
    K,
    Percent,
}

// ============================================================================
// Complex Types
// ============================================================================

/// EVSE identifier
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct EVSE {
    pub id: i32,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub connector_id: Option<i32>,
}

/// Token for identification
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct IdToken {
    pub id_token: String,
    #[serde(rename = "type")]
    pub token_type: String,
}

/// Charging schedule period
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ChargingSchedulePeriod {
    pub start_period: i32,
    pub limit: f64,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub number_phases: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub phase_to_use: Option<i32>,
}

/// Charging schedule
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ChargingSchedule {
    pub id: i32,
    pub charging_rate_unit: ChargingRateUnit,
    pub charging_schedule_period: Vec<ChargingSchedulePeriod>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub start_schedule: Option<DateTime<Utc>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub duration: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub min_charging_rate: Option<f64>,
}

/// Charging profile
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ChargingProfile {
    pub id: i32,
    pub stack_level: i32,
    pub charging_profile_purpose: ChargingProfilePurpose,
    pub charging_profile_kind: ChargingProfileKind,
    pub charging_schedule: Vec<ChargingSchedule>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub valid_from: Option<DateTime<Utc>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub valid_to: Option<DateTime<Utc>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub recurrency_kind: Option<RecurrencyKind>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub transaction_id: Option<String>,
}

/// Sampled value for meter readings
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SampledValue {
    pub value: f64,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub context: Option<ReadingContext>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub measurand: Option<Measurand>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub phase: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub unit_of_measure: Option<UnitOfMeasure>,
}

/// Meter value with timestamp and samples
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct MeterValue {
    pub timestamp: DateTime<Utc>,
    pub sampled_value: Vec<SampledValue>,
}

/// Status info for responses
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct StatusInfo {
    pub reason_code: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub additional_info: Option<String>,
}

// ============================================================================
// Request Messages
// ============================================================================

/// BootNotification request (CP -> CSMS)
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BootNotificationRequest {
    pub charging_station: ChargingStationInfo,
    pub reason: BootReason,
}

/// Charging station information
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ChargingStationInfo {
    pub model: String,
    pub vendor_name: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub serial_number: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub firmware_version: Option<String>,
}

/// Boot reason
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum BootReason {
    ApplicationReset,
    FirmwareUpdate,
    LocalReset,
    PowerUp,
    RemoteReset,
    ScheduledReset,
    Triggered,
    Unknown,
    Watchdog,
}

/// Heartbeat request (CP -> CSMS)
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HeartbeatRequest {}

/// StatusNotification request (CP -> CSMS)
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct StatusNotificationRequest {
    pub timestamp: DateTime<Utc>,
    pub connector_status: ConnectorStatus,
    pub evse_id: i32,
    pub connector_id: i32,
}

/// MeterValues request (CP -> CSMS)
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct MeterValuesRequest {
    pub evse_id: i32,
    pub meter_value: Vec<MeterValue>,
}

/// SetChargingProfile request (CSMS -> CP)
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SetChargingProfileRequest {
    pub evse_id: i32,
    pub charging_profile: ChargingProfile,
}

/// RequestStartTransaction request (CSMS -> CP)
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RequestStartTransactionRequest {
    pub id_token: IdToken,
    pub remote_start_id: i32,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub evse_id: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub charging_profile: Option<ChargingProfile>,
}

/// RequestStopTransaction request (CSMS -> CP)
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RequestStopTransactionRequest {
    pub transaction_id: String,
}

/// ReserveNow request (CSMS -> CP)
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ReserveNowRequest {
    pub id: i32,
    pub expiry_date_time: DateTime<Utc>,
    pub id_token: IdToken,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub evse_id: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub connector_type: Option<String>,
}

/// CancelReservation request (CSMS -> CP)
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CancelReservationRequest {
    pub reservation_id: i32,
}

// ============================================================================
// Response Messages
// ============================================================================

/// BootNotification response (CSMS -> CP)
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BootNotificationResponse {
    pub current_time: DateTime<Utc>,
    pub interval: i32,
    pub status: RegistrationStatus,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub status_info: Option<StatusInfo>,
}

/// Heartbeat response (CSMS -> CP)
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HeartbeatResponse {
    pub current_time: DateTime<Utc>,
}

/// StatusNotification response (CSMS -> CP)
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StatusNotificationResponse {}

/// MeterValues response (CSMS -> CP)
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MeterValuesResponse {}

/// SetChargingProfile response (CP -> CSMS)
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SetChargingProfileResponse {
    pub status: GenericStatus,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub status_info: Option<StatusInfo>,
}

/// RequestStartTransaction response (CP -> CSMS)
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RequestStartTransactionResponse {
    pub status: GenericStatus,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub transaction_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub status_info: Option<StatusInfo>,
}

/// RequestStopTransaction response (CP -> CSMS)
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RequestStopTransactionResponse {
    pub status: GenericStatus,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub status_info: Option<StatusInfo>,
}

/// ReserveNow response (CP -> CSMS)
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ReserveNowResponse {
    pub status: ReservationStatus,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub status_info: Option<StatusInfo>,
}

/// Reservation status
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum ReservationStatus {
    Accepted,
    Faulted,
    Occupied,
    Rejected,
    Unavailable,
}

/// CancelReservation response (CP -> CSMS)
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CancelReservationResponse {
    pub status: GenericStatus,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub status_info: Option<StatusInfo>,
}

// ============================================================================
// Composite State (for ROJ translation)
// ============================================================================

/// EVSE state as understood by ROJ
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EvseState {
    pub evse_id: i32,
    pub connector_id: i32,
    pub status: ConnectorStatus,
    pub active_session: Option<SessionState>,
    pub active_reservation: Option<ReservationState>,
    pub active_profile: Option<ChargingProfileState>,
    pub meter_values: Option<MeterState>,
}

/// Active charging session
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SessionState {
    pub transaction_id: String,
    pub id_token: String,
    pub started_at: DateTime<Utc>,
}

/// Active reservation
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ReservationState {
    pub reservation_id: i32,
    pub id_token: String,
    pub expiry: DateTime<Utc>,
}

/// Active charging profile state
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ChargingProfileState {
    pub profile_id: i32,
    pub stack_level: i32,
    pub limit_kw: f64,
}

/// Current meter state
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MeterState {
    pub power_kw: f64,
    pub energy_kwh: f64,
    pub soc_percent: Option<f64>,
    pub timestamp: DateTime<Utc>,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_charging_profile_serialization() {
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
                        limit: 22000.0,
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

        let json = serde_json::to_string(&profile).unwrap();
        assert!(json.contains("TxDefaultProfile"));
        assert!(json.contains("22000"));
    }

    #[test]
    fn test_boot_notification_request() {
        let req = BootNotificationRequest {
            charging_station: ChargingStationInfo {
                model: "EK3".to_string(),
                vendor_name: "Elektrokombinacija".to_string(),
                serial_number: Some("EK3-001".to_string()),
                firmware_version: Some("0.1.0".to_string()),
            },
            reason: BootReason::PowerUp,
        };

        let json = serde_json::to_string(&req).unwrap();
        let parsed: BootNotificationRequest = serde_json::from_str(&json).unwrap();
        assert_eq!(parsed.charging_station.model, "EK3");
    }
}
