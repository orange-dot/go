//! Elle-compatible history recording for consistency verification.
//!
//! This module provides types and utilities for recording transactional
//! histories in a format compatible with Elle (Jepsen's transactional
//! consistency checker).
//!
//! Elle uses a list-append model where operations are either:
//! - `append(key, value)` - append value to a list at key
//! - `r(key)` - read the list at key
//!
//! Each operation has three phases:
//! - `invoke` - operation started
//! - `ok` - operation completed successfully
//! - `fail` - operation failed or timed out

use parking_lot::RwLock;
use serde::{Deserialize, Serialize};
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::Instant;

/// Event type in Elle history
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum EventType {
    /// Operation invoked but not yet completed
    Invoke,
    /// Operation completed successfully
    Ok,
    /// Operation failed or timed out
    Fail,
    /// Informational event (not processed by Elle)
    Info,
}

impl std::fmt::Display for EventType {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            EventType::Invoke => write!(f, "invoke"),
            EventType::Ok => write!(f, "ok"),
            EventType::Fail => write!(f, "fail"),
            EventType::Info => write!(f, "info"),
        }
    }
}

/// Single micro-operation within a transaction
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(untagged)]
pub enum ElleOp {
    /// Append operation: ["append", key, value]
    Append {
        #[serde(rename = "0")]
        op: String, // "append"
        #[serde(rename = "1")]
        key: i64,
        #[serde(rename = "2")]
        value: i64,
    },
    /// Read operation: ["r", key, values] where values is null on invoke
    Read {
        #[serde(rename = "0")]
        op: String, // "r"
        #[serde(rename = "1")]
        key: i64,
        #[serde(rename = "2")]
        values: Option<Vec<i64>>,
    },
}

impl ElleOp {
    /// Create an append operation
    pub fn append(key: i64, value: i64) -> Self {
        ElleOp::Append {
            op: "append".to_string(),
            key,
            value,
        }
    }

    /// Create a read operation (for invoke, values is None)
    pub fn read(key: i64, values: Option<Vec<i64>>) -> Self {
        ElleOp::Read {
            op: "r".to_string(),
            key,
            values,
        }
    }

    /// Get the key for this operation
    pub fn key(&self) -> i64 {
        match self {
            ElleOp::Append { key, .. } => *key,
            ElleOp::Read { key, .. } => *key,
        }
    }

    /// Check if this is an append operation
    pub fn is_append(&self) -> bool {
        matches!(self, ElleOp::Append { .. })
    }

    /// Check if this is a read operation
    pub fn is_read(&self) -> bool {
        matches!(self, ElleOp::Read { .. })
    }

    /// Convert to Elle's JSON array format: ["op", key, value]
    pub fn to_elle_array(&self) -> serde_json::Value {
        match self {
            ElleOp::Append { key, value, .. } => {
                serde_json::json!(["append", key, value])
            }
            ElleOp::Read { key, values, .. } => {
                serde_json::json!(["r", key, values])
            }
        }
    }
}

/// A single event in the Elle history
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ElleEvent {
    /// Unique monotonic index for this event
    pub index: u64,
    /// Event type: invoke, ok, fail, info
    #[serde(rename = "type")]
    pub event_type: EventType,
    /// Function name (always "txn" for transactions)
    pub f: String,
    /// Process ID (node ID as integer)
    pub process: u64,
    /// Timestamp in nanoseconds since recorder start
    pub time: u64,
    /// Transaction value: list of micro-operations
    pub value: Vec<serde_json::Value>,
}

impl ElleEvent {
    /// Create a new Elle event
    pub fn new(
        index: u64,
        event_type: EventType,
        process: u64,
        time: u64,
        ops: Vec<ElleOp>,
    ) -> Self {
        Self {
            index,
            event_type,
            f: "txn".to_string(),
            process,
            time,
            value: ops.iter().map(|op| op.to_elle_array()).collect(),
        }
    }
}

/// History recorder for Elle-compatible output
pub struct HistoryRecorder {
    /// Recorded events
    events: RwLock<Vec<ElleEvent>>,
    /// Next event index (atomic for lock-free increment)
    next_index: AtomicU64,
    /// Process ID for this recorder
    process_id: u64,
    /// Start time for relative timestamps
    start_time: Instant,
    /// Enabled flag
    enabled: bool,
}

impl HistoryRecorder {
    /// Create a new history recorder
    pub fn new(process_id: u64) -> Self {
        Self {
            events: RwLock::new(Vec::new()),
            next_index: AtomicU64::new(0),
            process_id,
            start_time: Instant::now(),
            enabled: true,
        }
    }

    /// Create a disabled recorder (no-op)
    pub fn disabled() -> Self {
        Self {
            events: RwLock::new(Vec::new()),
            next_index: AtomicU64::new(0),
            process_id: 0,
            start_time: Instant::now(),
            enabled: false,
        }
    }

    /// Check if recording is enabled
    pub fn is_enabled(&self) -> bool {
        self.enabled
    }

    /// Get current timestamp in nanoseconds
    fn now_nanos(&self) -> u64 {
        self.start_time.elapsed().as_nanos() as u64
    }

    /// Record an invoke event (operation started)
    /// Returns the index for later matching with ok/fail
    pub fn invoke(&self, ops: Vec<ElleOp>) -> u64 {
        if !self.enabled {
            return 0;
        }

        let index = self.next_index.fetch_add(1, Ordering::SeqCst);
        let event = ElleEvent::new(
            index,
            EventType::Invoke,
            self.process_id,
            self.now_nanos(),
            ops,
        );

        self.events.write().push(event);
        index
    }

    /// Record a successful completion
    ///
    /// The `invoke_index` parameter is kept for API consistency with other
    /// Jepsen-style recorders, even though this implementation doesn't use it
    /// for matching (events are ordered by time instead).
    pub fn ok(&self, _invoke_index: u64, ops: Vec<ElleOp>) {
        if !self.enabled {
            return;
        }

        let index = self.next_index.fetch_add(1, Ordering::SeqCst);
        let event = ElleEvent::new(
            index,
            EventType::Ok,
            self.process_id,
            self.now_nanos(),
            ops,
        );

        self.events.write().push(event);
    }

    /// Record a failed/timed-out operation
    ///
    /// The `invoke_index` parameter is kept for API consistency with other
    /// Jepsen-style recorders, even though this implementation doesn't use it
    /// for matching (events are ordered by time instead).
    pub fn fail(&self, _invoke_index: u64) {
        if !self.enabled {
            return;
        }

        let index = self.next_index.fetch_add(1, Ordering::SeqCst);
        // For fail, we record empty ops (the invoke had the operation details)
        let event = ElleEvent::new(
            index,
            EventType::Fail,
            self.process_id,
            self.now_nanos(),
            vec![],
        );

        self.events.write().push(event);
    }

    /// Record an info event (not processed by Elle, for debugging)
    pub fn info(&self, message: &str) {
        if !self.enabled {
            return;
        }

        let index = self.next_index.fetch_add(1, Ordering::SeqCst);
        let event = ElleEvent {
            index,
            event_type: EventType::Info,
            f: "info".to_string(),
            process: self.process_id,
            time: self.now_nanos(),
            value: vec![serde_json::json!(message)],
        };

        self.events.write().push(event);
    }

    /// Get all recorded events
    pub fn events(&self) -> Vec<ElleEvent> {
        self.events.read().clone()
    }

    /// Clear all recorded events
    pub fn clear(&self) {
        self.events.write().clear();
        self.next_index.store(0, Ordering::SeqCst);
    }

    /// Get the number of recorded events
    pub fn len(&self) -> usize {
        self.events.read().len()
    }

    /// Check if no events have been recorded
    pub fn is_empty(&self) -> bool {
        self.events.read().is_empty()
    }

    /// Export history in Elle's JSON format
    pub fn export_json(&self) -> String {
        let events = self.events.read();
        serde_json::to_string_pretty(&*events).unwrap_or_else(|_| "[]".to_string())
    }

    /// Export history as EDN (for Clojure Elle)
    pub fn export_edn(&self) -> String {
        let events = self.events.read();
        let mut edn = String::from("[\n");

        for event in events.iter() {
            edn.push_str("  {:index ");
            edn.push_str(&event.index.to_string());
            edn.push_str(", :type :");
            edn.push_str(&event.event_type.to_string());
            edn.push_str(", :f :");
            edn.push_str(&event.f);
            edn.push_str(", :process ");
            edn.push_str(&event.process.to_string());
            edn.push_str(", :time ");
            edn.push_str(&event.time.to_string());
            edn.push_str(", :value [");

            for (i, op) in event.value.iter().enumerate() {
                if i > 0 {
                    edn.push_str(" ");
                }
                // Convert JSON array to EDN vector
                if let Some(arr) = op.as_array() {
                    edn.push('[');
                    for (j, item) in arr.iter().enumerate() {
                        if j > 0 {
                            edn.push(' ');
                        }
                        if let Some(s) = item.as_str() {
                            edn.push(':');
                            edn.push_str(s);
                        } else if item.is_null() {
                            edn.push_str("nil");
                        } else if let Some(arr_inner) = item.as_array() {
                            edn.push('[');
                            for (k, v) in arr_inner.iter().enumerate() {
                                if k > 0 {
                                    edn.push(' ');
                                }
                                edn.push_str(&v.to_string());
                            }
                            edn.push(']');
                        } else {
                            edn.push_str(&item.to_string());
                        }
                    }
                    edn.push(']');
                }
            }

            edn.push_str("]}\n");
        }

        edn.push(']');
        edn
    }
}

/// Helper to convert a string key to numeric key for Elle
pub fn key_to_numeric(key: &str) -> i64 {
    // Use first 8 bytes of key hash as numeric key
    use std::collections::hash_map::DefaultHasher;
    use std::hash::{Hash, Hasher};

    let mut hasher = DefaultHasher::new();
    key.hash(&mut hasher);
    (hasher.finish() & 0x7FFFFFFF) as i64
}

/// Helper to convert a JSON value to numeric for Elle
pub fn value_to_numeric(value: &serde_json::Value) -> i64 {
    match value {
        serde_json::Value::Number(n) => n.as_i64().unwrap_or(0),
        serde_json::Value::String(s) => key_to_numeric(s),
        serde_json::Value::Bool(b) => if *b { 1 } else { 0 },
        serde_json::Value::Null => 0,
        _ => {
            // Hash complex values
            let s = value.to_string();
            key_to_numeric(&s)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_elle_op_append() {
        let op = ElleOp::append(1, 42);
        assert!(op.is_append());
        assert!(!op.is_read());
        assert_eq!(op.key(), 1);

        let json = op.to_elle_array();
        assert_eq!(json, serde_json::json!(["append", 1, 42]));
    }

    #[test]
    fn test_elle_op_read() {
        let op = ElleOp::read(1, Some(vec![10, 20, 30]));
        assert!(!op.is_append());
        assert!(op.is_read());
        assert_eq!(op.key(), 1);

        let json = op.to_elle_array();
        assert_eq!(json, serde_json::json!(["r", 1, [10, 20, 30]]));
    }

    #[test]
    fn test_elle_op_read_invoke() {
        let op = ElleOp::read(1, None);
        let json = op.to_elle_array();
        assert_eq!(json, serde_json::json!(["r", 1, null]));
    }

    #[test]
    fn test_history_recorder_basic() {
        let recorder = HistoryRecorder::new(1);

        // Invoke append
        let idx = recorder.invoke(vec![ElleOp::append(1, 42)]);
        assert_eq!(idx, 0);

        // Complete with ok
        recorder.ok(idx, vec![ElleOp::append(1, 42)]);

        assert_eq!(recorder.len(), 2);

        let events = recorder.events();
        assert_eq!(events[0].event_type, EventType::Invoke);
        assert_eq!(events[1].event_type, EventType::Ok);
    }

    #[test]
    fn test_history_recorder_fail() {
        let recorder = HistoryRecorder::new(1);

        let idx = recorder.invoke(vec![ElleOp::append(1, 42)]);
        recorder.fail(idx);

        assert_eq!(recorder.len(), 2);

        let events = recorder.events();
        assert_eq!(events[0].event_type, EventType::Invoke);
        assert_eq!(events[1].event_type, EventType::Fail);
    }

    #[test]
    fn test_history_recorder_disabled() {
        let recorder = HistoryRecorder::disabled();

        let idx = recorder.invoke(vec![ElleOp::append(1, 42)]);
        assert_eq!(idx, 0);

        recorder.ok(idx, vec![ElleOp::append(1, 42)]);

        assert!(recorder.is_empty());
    }

    #[test]
    fn test_export_json() {
        let recorder = HistoryRecorder::new(1);

        recorder.invoke(vec![ElleOp::append(1, 42)]);

        let json = recorder.export_json();
        assert!(json.contains("invoke"));
        assert!(json.contains("txn"));
    }

    #[test]
    fn test_export_edn() {
        let recorder = HistoryRecorder::new(1);

        recorder.invoke(vec![ElleOp::append(1, 42)]);

        let edn = recorder.export_edn();
        assert!(edn.contains(":invoke"));
        assert!(edn.contains(":append"));
    }

    #[test]
    fn test_key_to_numeric_consistent() {
        let key1 = key_to_numeric("power_limit");
        let key2 = key_to_numeric("power_limit");
        assert_eq!(key1, key2);

        let key3 = key_to_numeric("temperature");
        assert_ne!(key1, key3);
    }

    #[test]
    fn test_value_to_numeric() {
        assert_eq!(value_to_numeric(&serde_json::json!(42)), 42);
        assert_eq!(value_to_numeric(&serde_json::json!(true)), 1);
        assert_eq!(value_to_numeric(&serde_json::json!(false)), 0);
        assert_eq!(value_to_numeric(&serde_json::json!(null)), 0);
    }

    #[test]
    fn test_clear() {
        let recorder = HistoryRecorder::new(1);

        recorder.invoke(vec![ElleOp::append(1, 42)]);
        assert_eq!(recorder.len(), 1);

        recorder.clear();
        assert!(recorder.is_empty());
    }
}
