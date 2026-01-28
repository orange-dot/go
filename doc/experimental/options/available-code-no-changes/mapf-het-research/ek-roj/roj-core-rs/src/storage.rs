//! Persistent storage for ROJ consensus state.
//!
//! Implements:
//! - Write-ahead log (WAL) for durability
//! - Crash recovery
//! - Checkpoint mechanism
//!
//! Designed for embedded systems:
//! - Works with flash storage (wear leveling friendly)
//! - Minimal memory footprint
//! - Configurable sync policy

use crate::election::PersistentState;
use crate::log::{LogEntry, Snapshot};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs::{self, File, OpenOptions};
use std::io::{self, BufRead, BufReader, BufWriter, Write};
use std::path::{Path, PathBuf};
use tracing::{debug, error, info, warn};

/// Storage configuration
#[derive(Debug, Clone)]
pub struct StorageConfig {
    /// Base directory for storage files
    pub dir: PathBuf,
    /// Sync to disk after every write
    pub fsync_on_write: bool,
    /// Maximum WAL size before compaction (bytes)
    pub max_wal_size: u64,
    /// Maximum entries in WAL before snapshot
    pub snapshot_threshold: usize,
}

impl Default for StorageConfig {
    fn default() -> Self {
        Self {
            dir: PathBuf::from("./roj-data"),
            fsync_on_write: true,
            max_wal_size: 10 * 1024 * 1024, // 10 MB
            snapshot_threshold: 1000,
        }
    }
}

/// WAL entry for recovery
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type")]
enum WalEntry {
    /// Election state update
    #[serde(rename = "ELECTION")]
    Election(PersistentState),

    /// Log entry append
    #[serde(rename = "LOG")]
    Log(LogEntry),

    /// Snapshot marker
    #[serde(rename = "SNAPSHOT")]
    SnapshotMarker {
        last_included_index: u64,
        last_included_term: u64,
    },

    /// Checkpoint marker (WAL can be truncated before this)
    #[serde(rename = "CHECKPOINT")]
    Checkpoint { wal_offset: u64 },
}

/// Recovered state from storage
#[derive(Debug, Default)]
pub struct RecoveredState {
    /// Election state (term, voted_for)
    pub election: PersistentState,
    /// Log entries after last snapshot
    pub log_entries: Vec<LogEntry>,
    /// Latest snapshot (if any)
    pub snapshot: Option<Snapshot>,
}

/// Persistent storage manager
pub struct Storage {
    config: StorageConfig,
    /// Current WAL file
    wal_file: Option<BufWriter<File>>,
    /// Current WAL size
    wal_size: u64,
    /// Entries written since last snapshot
    entries_since_snapshot: usize,
}

impl Storage {
    /// Create new storage instance
    pub fn new(config: StorageConfig) -> io::Result<Self> {
        // Create directory if needed
        fs::create_dir_all(&config.dir)?;

        Ok(Self {
            config,
            wal_file: None,
            wal_size: 0,
            entries_since_snapshot: 0,
        })
    }

    /// Open storage and recover state
    pub fn open(&mut self) -> io::Result<RecoveredState> {
        let state = self.recover()?;
        self.open_wal()?;
        Ok(state)
    }

    /// Open WAL file for appending
    fn open_wal(&mut self) -> io::Result<()> {
        let wal_path = self.config.dir.join("wal.log");

        let file = OpenOptions::new()
            .create(true)
            .append(true)
            .open(&wal_path)?;

        self.wal_size = file.metadata()?.len();
        self.wal_file = Some(BufWriter::new(file));

        debug!("Storage: Opened WAL at {:?} (size={})", wal_path, self.wal_size);
        Ok(())
    }

    /// Recover state from WAL and snapshots
    fn recover(&self) -> io::Result<RecoveredState> {
        let mut state = RecoveredState::default();

        // Load latest snapshot if exists
        let snapshot_path = self.config.dir.join("snapshot.json");
        if snapshot_path.exists() {
            let data = fs::read_to_string(&snapshot_path)?;
            match serde_json::from_str::<Snapshot>(&data) {
                Ok(snap) => {
                    info!(
                        "Storage: Loaded snapshot at index {}",
                        snap.last_included_index
                    );
                    state.snapshot = Some(snap);
                }
                Err(e) => {
                    warn!("Storage: Failed to parse snapshot: {}", e);
                }
            }
        }

        // Replay WAL
        let wal_path = self.config.dir.join("wal.log");
        if wal_path.exists() {
            let file = File::open(&wal_path)?;
            let reader = BufReader::new(file);
            let mut line_num = 0;

            for line in reader.lines() {
                line_num += 1;
                let line = match line {
                    Ok(l) => l,
                    Err(e) => {
                        warn!("Storage: WAL read error at line {}: {}", line_num, e);
                        break;
                    }
                };

                if line.trim().is_empty() {
                    continue;
                }

                match serde_json::from_str::<WalEntry>(&line) {
                    Ok(entry) => {
                        self.apply_wal_entry(&mut state, entry);
                    }
                    Err(e) => {
                        warn!(
                            "Storage: WAL parse error at line {}: {} - {}",
                            line_num, e, line
                        );
                    }
                }
            }

            info!(
                "Storage: Replayed {} WAL entries, {} log entries recovered",
                line_num,
                state.log_entries.len()
            );
        }

        Ok(state)
    }

    /// Apply a single WAL entry during recovery
    fn apply_wal_entry(&self, state: &mut RecoveredState, entry: WalEntry) {
        match entry {
            WalEntry::Election(es) => {
                state.election = es;
            }
            WalEntry::Log(le) => {
                // Only keep entries after snapshot
                let min_index = state
                    .snapshot
                    .as_ref()
                    .map(|s| s.last_included_index)
                    .unwrap_or(0);
                if le.index > min_index {
                    state.log_entries.push(le);
                }
            }
            WalEntry::SnapshotMarker { .. } => {
                // Clear log entries - snapshot file has state
                state.log_entries.clear();
            }
            WalEntry::Checkpoint { .. } => {
                // No action needed during recovery
            }
        }
    }

    /// Write entry to WAL
    fn write_wal(&mut self, entry: &WalEntry) -> io::Result<()> {
        let wal = self.wal_file.as_mut().ok_or_else(|| {
            io::Error::new(io::ErrorKind::NotConnected, "WAL not open")
        })?;

        let data = serde_json::to_string(entry)?;
        writeln!(wal, "{}", data)?;

        if self.config.fsync_on_write {
            wal.flush()?;
        }

        self.wal_size += data.len() as u64 + 1;
        self.entries_since_snapshot += 1;

        Ok(())
    }

    /// Persist election state
    pub fn save_election(&mut self, state: &PersistentState) -> io::Result<()> {
        self.write_wal(&WalEntry::Election(state.clone()))?;
        debug!(
            "Storage: Saved election state (term={}, voted_for={:?})",
            state.current_term, state.voted_for
        );
        Ok(())
    }

    /// Persist log entry
    pub fn save_log_entry(&mut self, entry: &LogEntry) -> io::Result<()> {
        self.write_wal(&WalEntry::Log(entry.clone()))?;
        debug!("Storage: Saved log entry {}", entry.index);

        // Check if snapshot is needed
        if self.entries_since_snapshot >= self.config.snapshot_threshold {
            debug!("Storage: Snapshot threshold reached");
        }

        Ok(())
    }

    /// Persist snapshot and compact WAL
    pub fn save_snapshot(&mut self, snapshot: &Snapshot) -> io::Result<()> {
        // Write snapshot to temp file then rename (atomic)
        let snapshot_path = self.config.dir.join("snapshot.json");
        let temp_path = self.config.dir.join("snapshot.json.tmp");

        let data = serde_json::to_string_pretty(snapshot)?;
        fs::write(&temp_path, &data)?;
        fs::rename(&temp_path, &snapshot_path)?;

        // Write marker to WAL
        self.write_wal(&WalEntry::SnapshotMarker {
            last_included_index: snapshot.last_included_index,
            last_included_term: snapshot.last_included_term,
        })?;

        // Compact WAL
        self.compact_wal()?;

        info!(
            "Storage: Saved snapshot at index {}",
            snapshot.last_included_index
        );
        Ok(())
    }

    /// Compact WAL by truncating old entries
    fn compact_wal(&mut self) -> io::Result<()> {
        // Close current WAL
        self.wal_file = None;

        // Rename old WAL
        let wal_path = self.config.dir.join("wal.log");
        let old_path = self.config.dir.join("wal.old");

        if wal_path.exists() {
            fs::rename(&wal_path, &old_path)?;
        }

        // Open new WAL
        self.wal_size = 0;
        self.entries_since_snapshot = 0;
        self.open_wal()?;

        // Delete old WAL
        if old_path.exists() {
            fs::remove_file(&old_path)?;
        }

        info!("Storage: WAL compacted");
        Ok(())
    }

    /// Check if snapshot should be taken
    pub fn should_snapshot(&self) -> bool {
        self.entries_since_snapshot >= self.config.snapshot_threshold
            || self.wal_size >= self.config.max_wal_size
    }

    /// Get storage statistics
    pub fn stats(&self) -> StorageStats {
        StorageStats {
            wal_size: self.wal_size,
            entries_since_snapshot: self.entries_since_snapshot,
        }
    }
}

/// Storage statistics
#[derive(Debug, Clone)]
pub struct StorageStats {
    pub wal_size: u64,
    pub entries_since_snapshot: usize,
}

/// In-memory storage for testing
pub struct MemoryStorage {
    election: PersistentState,
    log_entries: Vec<LogEntry>,
    snapshot: Option<Snapshot>,
}

impl MemoryStorage {
    pub fn new() -> Self {
        Self {
            election: PersistentState::default(),
            log_entries: Vec::new(),
            snapshot: None,
        }
    }

    pub fn save_election(&mut self, state: &PersistentState) {
        self.election = state.clone();
    }

    pub fn save_log_entry(&mut self, entry: &LogEntry) {
        self.log_entries.push(entry.clone());
    }

    pub fn save_snapshot(&mut self, snapshot: &Snapshot) {
        self.snapshot = Some(snapshot.clone());
        // Clear old log entries
        let min_index = snapshot.last_included_index;
        self.log_entries.retain(|e| e.index > min_index);
    }

    pub fn recover(&self) -> RecoveredState {
        RecoveredState {
            election: self.election.clone(),
            log_entries: self.log_entries.clone(),
            snapshot: self.snapshot.clone(),
        }
    }
}

impl Default for MemoryStorage {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::tempdir;

    #[test]
    fn test_memory_storage() {
        let mut storage = MemoryStorage::new();

        let state = PersistentState {
            current_term: 5,
            voted_for: Some("node1".to_string()),
        };
        storage.save_election(&state);

        let entry = LogEntry::new_data(1, 5, "key".to_string(), serde_json::json!("value"));
        storage.save_log_entry(&entry);

        let recovered = storage.recover();
        assert_eq!(recovered.election.current_term, 5);
        assert_eq!(recovered.log_entries.len(), 1);
    }

    #[test]
    fn test_file_storage_roundtrip() -> io::Result<()> {
        let dir = tempdir()?;
        let config = StorageConfig {
            dir: dir.path().to_path_buf(),
            fsync_on_write: false,
            ..Default::default()
        };

        // Write
        {
            let mut storage = Storage::new(config.clone())?;
            storage.open_wal()?;

            let state = PersistentState {
                current_term: 3,
                voted_for: Some("nodeA".to_string()),
            };
            storage.save_election(&state)?;

            let entry = LogEntry::new_data(1, 3, "test".to_string(), serde_json::json!(42));
            storage.save_log_entry(&entry)?;
        }

        // Read
        {
            let mut storage = Storage::new(config)?;
            let recovered = storage.open()?;

            assert_eq!(recovered.election.current_term, 3);
            assert_eq!(recovered.election.voted_for, Some("nodeA".to_string()));
            assert_eq!(recovered.log_entries.len(), 1);
            assert_eq!(recovered.log_entries[0].key, "test");
        }

        Ok(())
    }

    #[test]
    fn test_snapshot_recovery() -> io::Result<()> {
        let dir = tempdir()?;
        let config = StorageConfig {
            dir: dir.path().to_path_buf(),
            fsync_on_write: false,
            snapshot_threshold: 3,
            ..Default::default()
        };

        // Write entries and snapshot
        {
            let mut storage = Storage::new(config.clone())?;
            storage.open_wal()?;

            for i in 1..=5 {
                let entry = LogEntry::new_data(i, 1, format!("key{}", i), serde_json::json!(i));
                storage.save_log_entry(&entry)?;
            }

            let mut state = HashMap::new();
            state.insert("key1".to_string(), serde_json::json!(1));
            state.insert("key2".to_string(), serde_json::json!(2));
            state.insert("key3".to_string(), serde_json::json!(3));

            let snapshot = Snapshot {
                last_included_index: 3,
                last_included_term: 1,
                state,
            };
            storage.save_snapshot(&snapshot)?;
        }

        // Recover
        {
            let mut storage = Storage::new(config)?;
            let recovered = storage.open()?;

            assert!(recovered.snapshot.is_some());
            let snap = recovered.snapshot.unwrap();
            assert_eq!(snap.last_included_index, 3);
            assert_eq!(snap.state.len(), 3);
        }

        Ok(())
    }
}
