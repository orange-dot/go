# Elle Integration for ROJ Consensus

This document describes the integration of [Elle](https://github.com/jepsen-io/elle) (Jepsen's transactional consistency checker) with ROJ consensus for black-box history analysis and consistency verification.

## Overview

Elle is a black-box transactional consistency checker that can detect anomalies like G0 (write-write conflicts), G1a/b/c (various read anomalies), G2 (anti-dependency cycles), lost updates, and dirty reads. By instrumenting ROJ consensus to emit Elle-compatible histories, we can verify that our distributed consensus implementation maintains the consistency guarantees it claims.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    roj-elle-harness                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌─────────────┐ │
│  │ Cluster  │  │ Workload │  │  Fault   │  │ Elle Runner │ │
│  │ Manager  │  │Generator │  │ Injector │  │  (JVM CLI)  │ │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └──────┬──────┘ │
└───────┼─────────────┼─────────────┼───────────────┼────────┘
        │             │             │               │
        ▼             ▼             ▼               │
┌─────────────────────────────────────────────┐    │
│              roj-core-rs [elle]             │    │
│  ┌───────────┐  ┌─────────┐  ┌───────────┐ │    │
│  │ consensus │  │   log   │  │  history  │ │    │
│  │ (instrum) │  │(instrum)│  │ (recorder)│─┼────┘
│  └───────────┘  └─────────┘  └───────────┘ │  JSON export
└─────────────────────────────────────────────┘
```

## Implementation Details

### 1. History Recording Module (`roj-core-rs/src/history.rs`)

The history module provides Elle-compatible event recording:

#### Types

```rust
/// Event type in Elle history
pub enum EventType {
    Invoke,  // Operation started
    Ok,      // Operation completed successfully
    Fail,    // Operation failed or timed out
    Info,    // Informational (not processed by Elle)
}

/// Single micro-operation within a transaction
pub enum ElleOp {
    Append { key: i64, value: i64 },        // ["append", key, value]
    Read { key: i64, values: Option<Vec<i64>> }, // ["r", key, [values]]
}

/// A single event in the Elle history
pub struct ElleEvent {
    pub index: u64,
    pub event_type: EventType,
    pub f: String,           // "txn"
    pub process: u64,        // Node ID
    pub time: u64,           // Nanoseconds since start
    pub value: Vec<Value>,   // List of micro-operations
}
```

#### HistoryRecorder

```rust
pub struct HistoryRecorder {
    events: RwLock<Vec<ElleEvent>>,
    next_index: AtomicU64,
    process_id: u64,
    start_time: Instant,
    enabled: bool,
}

impl HistoryRecorder {
    pub fn new(process_id: u64) -> Self;
    pub fn disabled() -> Self;

    pub fn invoke(&self, ops: Vec<ElleOp>) -> u64;  // Returns invoke index
    pub fn ok(&self, invoke_index: u64, ops: Vec<ElleOp>);
    pub fn fail(&self, invoke_index: u64);

    pub fn export_json(&self) -> String;  // Elle JSON format
    pub fn export_edn(&self) -> String;   // Clojure EDN format
}
```

#### Helper Functions

```rust
/// Convert string key to numeric (Elle uses numeric keys)
pub fn key_to_numeric(key: &str) -> i64;

/// Convert JSON value to numeric
pub fn value_to_numeric(value: &serde_json::Value) -> i64;
```

### 2. Feature Flag

The Elle integration is gated behind the `elle` feature flag to avoid overhead in production:

```toml
# roj-core-rs/Cargo.toml
[features]
default = []
elle = ["parking_lot"]

[dependencies]
parking_lot = { version = "0.12", optional = true }
```

### 3. Consensus Instrumentation (`roj-core-rs/src/consensus.rs`)

The consensus module is instrumented at key points:

| Method | Event | Description |
|--------|-------|-------------|
| `create_proposal()` | `invoke` | Records when a proposal is created |
| `handle_vote()` (commit) | `ok` | Records when proposal reaches quorum and commits |
| `cleanup_expired()` | `fail` | Records when proposal times out |

```rust
pub struct Consensus {
    // ... existing fields ...
    #[cfg(feature = "elle")]
    history: Option<Arc<HistoryRecorder>>,
    #[cfg(feature = "elle")]
    pending_invokes: HashMap<String, u64>,  // proposal_id -> invoke_index
}
```

### 4. Log Instrumentation (`roj-core-rs/src/log.rs`)

The replicated log is instrumented for append operations:

| Method | Event | Description |
|--------|-------|-------------|
| `append()` | `invoke` | Records when entry is appended to log |
| `apply_committed()` | `ok` | Records when entry is applied to state machine |

```rust
pub struct ReplicatedLog {
    // ... existing fields ...
    #[cfg(feature = "elle")]
    history: Option<Arc<HistoryRecorder>>,
    #[cfg(feature = "elle")]
    pending_invokes: HashMap<u64, u64>,  // log_index -> invoke_index
}
```

### 5. Test Harness (`roj-elle-harness/`)

The harness provides an end-to-end testing framework:

#### CLI Commands

```bash
# Run a single scenario
roj-elle run --scenario <name> --nodes <n> --operations <n> --output-dir <dir>

# Check history with Elle
roj-elle check --history <file> --model list-append --elle-jar <path>

# Run full test suite
roj-elle suite --output-dir <dir> --junit-xml <file> --elle-jar <path>

# Download elle-cli JAR
roj-elle download --target <dir>
```

#### Scenarios

| Scenario | Description | Nodes |
|----------|-------------|-------|
| `happy` | No faults - baseline test | 3 |
| `partition` | Network partition mid-test | 5 |
| `leader-crash` | Leader crashes mid-test | 5 |
| `message-loss` | 10% message loss throughout | 5 |
| `contention` | Single key, high contention | 3 |

#### Cluster Module (`cluster.rs`)

In-process multi-node cluster with channel-based messaging:

- `ClusterNode` - Single node with state machine and history recorder
- `Cluster` - Manages multiple nodes, handles message routing
- Supports network partitions via partition matrix
- Supports node crashes via `crashed` flag

#### Fault Injection (`fault_injection.rs`)

- `FaultConfig` - Configuration for fault injection
- `FaultInjector` - Injects partitions, crashes, message loss
- `FaultScenario` - Predefined fault scenarios

#### Workload Generation (`workload.rs`)

- `WorkloadGenerator` - Generates append/read operations
- `WorkloadConfig` - Configures key count, append ratio
- Supports various patterns (write-heavy, mixed, read-heavy)

#### Elle Runner (`elle_runner.rs`)

- Downloads elle-cli JAR if missing
- Spawns Java process to run Elle
- Parses Elle output for anomaly detection
- Returns `ElleResult` with validity and anomaly type

### 6. Go Simulator Integration

The Elle harness can also run tests against the Go simulator, which provides realistic CAN-FD bus simulation instead of in-memory channels.

#### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    roj-elle-harness (Rust)                  │
│  ┌───────────────┐  ┌──────────────┐  ┌─────────────────┐  │
│  │   Simulator   │  │   Scenario   │  │   Elle Runner   │  │
│  │    Adapter    │  │   Executor   │  │    (JVM CLI)    │  │
│  └───────┬───────┘  └──────┬───────┘  └────────┬────────┘  │
└──────────┼─────────────────┼───────────────────┼───────────┘
           │ HTTP            │                   │
           ▼                 ▼                   │
┌─────────────────────────────────────────────────────────┐
│                    Go Simulator                          │
│  ┌───────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │  ROJ Cluster  │  │   API Server │  │   CAN-FD Bus │  │
│  │   (Go impl)   │  │   :8001      │  │  Simulation  │  │
│  └───────────────┘  └──────────────┘  └──────────────┘  │
└─────────────────────────────────────────────────────────┘
```

#### Go-side Implementation (`simulator/engine/internal/roj/`)

**consensus.go** - ROJ consensus node implementing `CANNode` interface:
- Handles ROJ_Propose, ROJ_Vote, ROJ_Commit CAN messages
- Records Elle history events (invoke, ok, fail)
- 2/3 quorum voting threshold

**cluster.go** - Cluster manager:
- Partition matrix for network fault simulation
- Node crash/recovery support
- History aggregation across all nodes

**api.go** - HTTP API for external control:
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/health` | GET | Health check |
| `/api/roj/status` | GET | Cluster status |
| `/api/roj/stats` | GET | Cluster statistics |
| `/api/roj/propose` | POST | Submit proposal via node |
| `/api/roj/state` | GET | Get node committed state |
| `/api/roj/crash` | POST | Crash a node |
| `/api/roj/recover` | POST | Recover a crashed node |
| `/api/roj/partition` | POST | Create network partition |
| `/api/roj/heal` | POST | Heal all partitions |
| `/api/roj/history` | GET | Get Elle history |
| `/api/roj/history/clear` | POST | Clear history |
| `/api/roj/scenario/run` | POST | Run predefined scenario |

#### Rust-side Adapter (`simulator_adapter.rs`)

```rust
pub struct SimulatorAdapter {
    client: reqwest::Client,
    base_url: String,
}

impl SimulatorAdapter {
    pub async fn health_check(&self) -> Result<bool, SimulatorError>;
    pub async fn wait_for_ready(&self, timeout: Duration) -> Result<(), SimulatorError>;
    pub async fn propose(&self, node_id: usize, key: &str, value: Value) -> Result<String, SimulatorError>;
    pub async fn crash_node(&self, node_id: usize) -> Result<(), SimulatorError>;
    pub async fn recover_node(&self, node_id: usize) -> Result<(), SimulatorError>;
    pub async fn partition(&self, group_a: Vec<usize>, group_b: Vec<usize>) -> Result<(), SimulatorError>;
    pub async fn heal_partitions(&self) -> Result<(), SimulatorError>;
    pub async fn get_history_json(&self) -> Result<String, SimulatorError>;
    pub async fn run_scenario(&self, scenario: &str, nodes: usize, operations: usize) -> Result<(), SimulatorError>;
}
```

#### CLI Command

```bash
# Run Elle tests via Go simulator
roj-elle sim --scenario happy --nodes 5 --operations 100 \
    --simulator-url http://localhost:8001 \
    --output-dir ./results \
    --timeout 10

# Available scenarios via simulator
# - happy: No faults, all nodes healthy
# - partition: 2-3 network split mid-test
# - leader-crash: Node 0 crashes mid-test
# - message-loss: Simulated at CAN bus level
```

#### Running Simulator Tests

```bash
# Start Go simulator (in separate terminal)
cd simulator/engine
go run ./cmd/simulator

# Run Elle tests via simulator
cd mapf-het-research/ek-roj
./target/release/roj-elle sim --scenario partition --nodes 5 --operations 100

# Output:
# History written to: ./results/sim-partition-history.json
# Summary written to: ./results/sim-partition-summary.json
# Elle Analysis Result:
#   Valid: true
```

### 7. CI Integration

#### GitHub Actions (`.github/workflows/elle-tests.yml`)

```yaml
name: Elle Consistency Tests

on:
  push:
    branches: [main, ek-roj-dev]
    paths: ['ek-roj/**']
  schedule:
    - cron: '0 3 * * *'  # Nightly

jobs:
  elle-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: dtolnay/rust-action@stable
      - uses: actions/setup-java@v4
        with:
          java-version: '17'
      - name: Build harness
        run: cargo build --release -p roj-elle-harness
      - name: Run Elle test suite
        run: ./target/release/roj-elle suite --output-dir ./results
```

#### Local Scripts

- `scripts/run-elle-tests.sh` - Linux/macOS
- `scripts/run-elle-tests.bat` - Windows

## Usage

### Building with Elle Support

```bash
cd mapf-het-research/ek-roj

# Check compilation
cargo check -p roj-core --features elle

# Run tests
cargo test -p roj-core --features elle
cargo test -p roj-elle-harness

# Build release harness
cargo build --release -p roj-elle-harness
```

### Running Tests Locally

```bash
# Using the script (downloads elle-cli automatically)
./scripts/run-elle-tests.sh

# Or manually:
./target/release/roj-elle download
./target/release/roj-elle run --scenario happy --nodes 3 --operations 100
./target/release/roj-elle check --history results/happy-history.json --model list-append
```

### Interpreting Results

Elle will report one of:
- **Valid** - No anomalies detected
- **Anomaly detected** - Specific anomaly type:
  - G0: Write-write conflict
  - G1a: Aborted read
  - G1b: Intermediate read
  - G1c: Circular information flow
  - G2: Anti-dependency cycle
  - Lost update
  - Dirty read

## File Structure

```
ek-roj/
├── roj-core-rs/
│   ├── Cargo.toml              # Added 'elle' feature
│   └── src/
│       ├── lib.rs              # Exports history module
│       ├── history.rs          # NEW: Elle types + recorder
│       ├── consensus.rs        # Instrumented with Elle events
│       └── log.rs              # Instrumented with Elle events
├── roj-elle-harness/           # NEW CRATE
│   ├── Cargo.toml
│   └── src/
│       ├── main.rs             # CLI entry point
│       ├── cluster.rs          # In-process cluster
│       ├── fault_injection.rs  # Fault injection
│       ├── workload.rs         # Workload generation
│       ├── elle_runner.rs      # Elle CLI wrapper
│       ├── scenarios.rs        # Predefined scenarios
│       └── simulator_adapter.rs # NEW: Go simulator adapter
├── scripts/
│   ├── run-elle-tests.sh       # NEW: Linux/macOS script
│   └── run-elle-tests.bat      # NEW: Windows script
└── .github/
    └── workflows/
        └── elle-tests.yml      # NEW: CI workflow

simulator/engine/internal/roj/  # Go simulator ROJ implementation
├── consensus.go                # ROJ consensus node (CANNode impl)
├── cluster.go                  # Cluster manager with partitions
└── api.go                      # HTTP API for external control
```

## Dependencies

### Rust
- `parking_lot` - Fast RwLock for history recorder (optional, elle feature)
- `tempfile` - Temporary files for test histories

### External
- Java 11+ runtime
- [elle-cli](https://github.com/ligurio/elle-cli) JAR (~20MB)

## Test Results

As of implementation:
- **roj-core tests with elle feature**: 34 passed
- **roj-elle-harness tests**: 19 passed (includes simulator adapter tests)
- **Go simulator build**: Compiles successfully

## Advanced Features (Implemented)

The following advanced features have been implemented to enhance Elle testing capabilities:

### 1. Slow Network Scenario

Configurable message delays to test timeout behavior and network latency resilience.

#### Configuration

```rust
// In fault_injection.rs
pub struct FaultConfig {
    // ... existing fields ...
    /// Minimum message delay in milliseconds (0 = no delay)
    pub min_delay_ms: u64,
    /// Maximum message delay in milliseconds
    pub max_delay_ms: u64,
}

impl FaultConfig {
    /// Slow network preset (100-500ms delays)
    pub fn slow_network() -> Self {
        Self {
            min_delay_ms: 100,
            max_delay_ms: 500,
            ..Default::default()
        }
    }
}
```

#### Usage

```bash
# Run slow network scenario
roj-elle run --scenario slow-network --nodes 5 --operations 50

# Via Go simulator
roj-elle sim --scenario slow-network --nodes 5 --operations 50
```

#### Go Simulator Support

```go
// In cluster.go
type ClusterConfig struct {
    // ... existing fields ...
    MinDelayMs int // Minimum message delay in milliseconds
    MaxDelayMs int // Maximum message delay in milliseconds
}

// Set network delay
func (c *Cluster) SetNetworkDelay(minMs, maxMs int)
func (c *Cluster) ClearNetworkDelay()
```

#### Verification

- Proposals should still commit within 10s timeout
- History shows increased time gaps between invoke/ok events
- Consensus maintains correctness despite delays

---

### 2. Performance Profiling

Collect latency histograms and throughput metrics, export to local JSON/CSV files.

#### Metrics Module (`metrics.rs`)

```rust
/// Performance metrics collection
pub struct Metrics {
    /// Proposal-to-commit latencies in microseconds
    pub latencies_us: Vec<u64>,
    /// Operations per second samples
    pub throughput_samples: Vec<f64>,
    /// Message counts by type
    pub message_counts: HashMap<String, u64>,
}

/// Summary statistics
pub struct MetricsSummary {
    pub min_latency_us: u64,
    pub max_latency_us: u64,
    pub avg_latency_us: f64,
    pub p50_latency_us: u64,
    pub p95_latency_us: u64,
    pub p99_latency_us: u64,
    pub total_ops: usize,
    pub avg_throughput_ops_sec: f64,
}

impl Metrics {
    pub fn new() -> Self;
    pub fn start(&mut self);  // Start timing
    pub fn record_latency(&mut self, proposal_time: Instant, commit_time: Instant);
    pub fn record_latency_duration(&mut self, latency: Duration);
    pub fn sample_throughput(&mut self);  // Call periodically
    pub fn count_message(&mut self, msg_type: &str);
    pub fn summary(&self) -> MetricsSummary;
    pub fn export_json(&self, scenario: &str) -> String;
    pub fn export_csv(&self) -> String;
}
```

#### Usage

```bash
# Run with metrics collection
roj-elle run --scenario happy --operations 100 --metrics-file metrics.json

# Output:
# Performance Metrics:
#   Total operations: 85
#   Latency (min/avg/max): 1234/3500/9800 us
#   Latency p50/p95/p99: 3200/8500/9500 us
#   Throughput: 85.5 ops/sec
```

#### Output Format (JSON)

```json
{
  "scenario": "happy",
  "latencies_us": [1234, 5678, ...],
  "throughput_samples": [82.5, 88.3, ...],
  "message_counts": {
    "Propose": 100,
    "Vote": 400,
    "Commit": 95
  },
  "summary": {
    "min_latency_us": 1000,
    "max_latency_us": 10000,
    "avg_latency_us": 3500.0,
    "p50_latency_us": 3200,
    "p95_latency_us": 8500,
    "p99_latency_us": 9800,
    "total_ops": 95,
    "avg_throughput_ops_sec": 85.5
  }
}
```

#### Comparison Example

```bash
# Compare happy path vs slow network
roj-elle run --scenario happy --operations 100 --metrics-file happy-metrics.json
roj-elle run --scenario slow-network --operations 100 --metrics-file slow-metrics.json

# Use jq to compare
jq '.summary.avg_latency_us' happy-metrics.json slow-metrics.json
```

---

### 3. WebSocket Streaming

Real-time history event streaming from Go simulator to clients.

#### Go Server-Side (`api.go`)

```go
// WebSocket endpoint
mux.HandleFunc("/api/roj/history/stream", s.handleHistoryStream)

// Handler implementation
func (s *APIServer) handleHistoryStream(w http.ResponseWriter, r *http.Request) {
    // Upgrade HTTP to WebSocket
    conn, err := wsUpgrader.Upgrade(w, r, nil)
    if err != nil {
        return
    }
    defer conn.Close()

    // Subscribe to cluster events
    eventChan := s.cluster.Subscribe()
    defer s.cluster.Unsubscribe(eventChan)

    // Stream events to client
    for event := range eventChan {
        data, _ := json.Marshal(event)
        conn.WriteMessage(websocket.TextMessage, data)
    }
}
```

#### Cluster Event Broadcasting (`cluster.go`)

```go
// Subscribe to events
func (c *Cluster) Subscribe() chan ElleEvent {
    ch := make(chan ElleEvent, 100)
    c.subscribersMu.Lock()
    c.subscribers = append(c.subscribers, ch)
    c.subscribersMu.Unlock()
    return ch
}

// Unsubscribe from events
func (c *Cluster) Unsubscribe(ch chan ElleEvent)

// Broadcast event to all subscribers
func (c *Cluster) BroadcastEvent(event ElleEvent) {
    c.subscribersMu.RLock()
    defer c.subscribersMu.RUnlock()
    for _, ch := range c.subscribers {
        select {
        case ch <- event:
        default: // Drop if channel full
        }
    }
}
```

#### Rust Client-Side (`simulator_adapter.rs`)

```rust
impl SimulatorAdapter {
    /// Get WebSocket URL
    pub fn websocket_url(&self) -> String {
        // ws://localhost:8001/api/roj/history/stream
    }

    /// Stream history events via WebSocket
    pub async fn stream_history(&self)
        -> Result<tokio::sync::mpsc::Receiver<SimulatorElleEvent>, SimulatorError>;

    /// Stream with callback (stops when callback returns false)
    pub async fn stream_history_with_callback<F>(&self, callback: F)
        -> Result<(), SimulatorError>
    where
        F: FnMut(SimulatorElleEvent) -> bool;
}
```

#### Usage

```bash
# Start simulator
cd simulator/engine && go run ./cmd/simulator

# Connect via wscat (for testing)
wscat -c ws://localhost:8001/api/roj/history/stream

# Run scenario (events stream in real-time)
roj-elle sim --scenario partition --nodes 5

# Example event output:
# {"index":0,"type":"invoke","f":"txn","process":0,"time":123456,"value":[["append",1,1]]}
# {"index":1,"type":"ok","f":"txn","process":0,"time":234567,"value":[["append",1,1]]}
```

#### Dependencies

**Rust (Cargo.toml):**
```toml
tokio-tungstenite = "0.21"
futures-util = "0.3"
```

**Go (go.mod):**
```
github.com/gorilla/websocket v1.5.1
```

---

### 4. Graph Visualization (D3.js Web App)

Interactive anomaly graphs and timeline visualization in the web application.

#### Components

| Component | File | Description |
|-----------|------|-------------|
| `HistoryUploader` | `web/src/components/elle/HistoryUploader.jsx` | Drag-and-drop JSON upload |
| `TimelineView` | `web/src/components/elle/TimelineView.jsx` | Swimlane timeline (D3.js) |
| `AnomalyGraph` | `web/src/components/elle/AnomalyGraph.jsx` | Force-directed dependency graph |
| `ElleVisualizerPage` | `web/src/pages/ElleVisualizerPage.jsx` | Main visualization page |

#### HistoryUploader

```jsx
import HistoryUploader from '../components/elle/HistoryUploader';

<HistoryUploader onUpload={(history, fileName) => {
  console.log(`Loaded ${history.length} events from ${fileName}`);
}} />
```

Features:
- Drag-and-drop support
- Click to browse
- JSON validation
- Error handling

#### TimelineView

```jsx
import TimelineView from '../components/elle/TimelineView';

<TimelineView
  history={history}      // Array of Elle events
  anomalies={anomalies}  // Optional anomaly highlights
  height={400}           // SVG height
/>
```

Features:
- Horizontal swimlanes (one per process/node)
- X-axis: time in milliseconds
- Invoke events: hollow blue circles
- Ok events: filled green circles
- Fail events: filled red circles
- Operation spans connecting invoke → completion

#### AnomalyGraph

```jsx
import AnomalyGraph from '../components/elle/AnomalyGraph';

<AnomalyGraph
  history={history}
  anomalies={anomalies}  // Anomaly cycles to highlight in red
  height={500}
/>
```

Features:
- Force-directed layout (D3.js simulation)
- Nodes colored by event type (invoke/ok/fail)
- Edges show dependencies (completion, cross-process)
- Anomaly cycles highlighted in red
- Interactive drag to rearrange nodes
- Tooltips with event details

#### Page Route

```jsx
// web/src/App.jsx
import ElleVisualizerPage from './pages/ElleVisualizerPage';

<Route path="/elle" element={<ElleVisualizerPage />} />
```

#### Usage

1. Build and start the web app:
   ```bash
   cd web
   npm install  # Installs d3
   npm run dev
   ```

2. Navigate to `http://localhost:5173/elle`

3. Upload history file or connect to simulator WebSocket

4. Switch between Timeline and Dependency Graph views

---

### 5. Continuous Monitoring Dashboard

Live dashboard showing scenario progress and real-time metrics.

#### Components

| Component | File | Description |
|-----------|------|-------------|
| `MonitoringDashboard` | `web/src/components/elle/MonitoringDashboard.jsx` | Live metrics cards |
| `ScenarioProgress` | `web/src/components/elle/ScenarioProgress.jsx` | Progress bar |
| `EventLog` | `web/src/components/elle/EventLog.jsx` | Real-time event log |

#### MonitoringDashboard

```jsx
import MonitoringDashboard from '../components/elle/MonitoringDashboard';

<MonitoringDashboard
  events={streamedEvents}  // Array of events (live or uploaded)
  connected={wsConnected}  // WebSocket connection status
/>
```

Displays:
- Connection status (Live/Disconnected)
- Event count (invokes/commits)
- Success rate percentage
- Average latency with sparkline trend

#### ScenarioProgress

```jsx
import ScenarioProgress from '../components/elle/ScenarioProgress';

<ScenarioProgress
  scenario="partition"     // Scenario name
  expectedOps={100}        // Expected operation count
  events={events}          // Current events
  isRunning={true}         // Whether scenario is active
/>
```

Displays:
- Scenario name and status
- Progress percentage
- Segmented progress bar (ok/fail/pending)
- Legend with counts

#### EventLog

```jsx
import EventLog from '../components/elle/EventLog';

<EventLog
  events={events}     // Array of events
  maxItems={30}       // Max items to display
/>
```

Features:
- Auto-scroll to newest events
- Color-coded by event type
- Shows process ID, type, operation, time
- Compact format for high event rates

#### Integration

The monitoring components are integrated into ElleVisualizerPage under the "Live Monitor" tab:

```jsx
// In ElleVisualizerPage.jsx
{activeTab === TABS.MONITOR && (
  <div className="space-y-6">
    <MonitoringDashboard events={events} connected={wsConnected} />
    <div className="grid grid-cols-2 gap-6">
      <ScenarioProgress scenario={scenario} expectedOps={100} events={events} isRunning={wsConnected} />
      <EventLog events={events} maxItems={30} />
    </div>
  </div>
)}
```

---

### 6. Byzantine Fault Tolerance Testing

Full BFT testing with equivocation detection and vote validation.

#### Byzantine Behavior Types

```rust
// In fault_injection.rs
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ByzantineBehavior {
    /// Normal (honest) behavior
    Honest,
    /// Send conflicting votes to different nodes
    Equivocating,
    /// Broadcast false commits without quorum
    FalseCommit,
    /// Don't respond to proposals (silent)
    Silent,
    /// Send invalid/malformed messages
    Malformed,
}
```

#### BFT Configuration

```rust
impl FaultConfig {
    /// Byzantine with equivocating node
    pub fn bft_equivocation(byzantine_node: usize) -> Self {
        let mut byzantine_nodes = HashMap::new();
        byzantine_nodes.insert(byzantine_node, ByzantineBehavior::Equivocating);
        Self { byzantine_nodes, ..Default::default() }
    }

    /// Byzantine minority (f < n/3)
    pub fn bft_minority(num_byzantine: usize, total_nodes: usize) -> Self;

    /// Byzantine false commit node
    pub fn bft_false_commit(byzantine_node: usize) -> Self;

    /// Check if a node is Byzantine
    pub fn is_byzantine(&self, node_idx: usize) -> bool;

    /// Get Byzantine behavior for a node
    pub fn get_byzantine_behavior(&self, node_idx: usize) -> ByzantineBehavior;
}
```

#### BFT Scenarios

| Scenario | Byzantine Nodes | Expected Outcome |
|----------|-----------------|------------------|
| `bft-equivocation` | 1 of 5 equivocating | Should commit (4 honest) |
| `bft-minority` | 1 of 7 Byzantine | Should commit (f < n/3) |
| `bft-threshold` | 2 of 7 Byzantine | Should commit (f = n/3) |
| `bft-false-commit` | 1 false committer | Honest nodes reject |

#### Equivocation Detection (`cluster.rs`)

```rust
impl ClusterNode {
    /// Set of nodes detected as Byzantine
    pub known_byzantine: HashSet<NodeId>,
    /// Track votes per proposal per node
    pub vote_history: HashMap<String, HashMap<NodeId, Vote>>,

    /// Check for equivocation in vote handling
    fn handle_vote(&mut self, from: NodeId, proposal_id: &str, vote: Vote) {
        // Check vote history for this proposal
        if let Some(existing_vote) = self.vote_history
            .get(&proposal_id)
            .and_then(|m| m.get(&from))
        {
            if *existing_vote != vote {
                // Equivocation detected!
                warn!("Equivocation from {}: had {:?}, now {:?}", from, existing_vote, vote);
                self.mark_byzantine(from);
                return; // Ignore this vote
            }
        }
        // Record vote in history
        self.vote_history
            .entry(proposal_id.to_string())
            .or_default()
            .insert(from, vote);
    }

    /// Only count votes from non-Byzantine nodes for quorum
    fn check_quorum(&self, proposal: &Proposal) -> bool {
        let honest_accepts = proposal.votes.iter()
            .filter(|(node, vote)| {
                **vote == Vote::Accept && !self.known_byzantine.contains(*node)
            })
            .count();
        honest_accepts >= self.quorum_threshold()
    }
}
```

#### BFT Threshold Calculation

```rust
impl FaultScenario {
    /// BFT requires f < n/3 Byzantine nodes
    /// max f = floor((n-1)/3)
    pub fn max_byzantine_tolerated(&self) -> usize {
        let n = self.recommended_nodes();
        (n - 1) / 3
    }
}

// Examples:
// 5 nodes: can tolerate f=1 ((5-1)/3 = 1)
// 7 nodes: can tolerate f=2 ((7-1)/3 = 2)
```

#### Go Simulator BFT Support

```go
// Byzantine behavior types
type ByzantineBehavior int

const (
    ByzantineHonest ByzantineBehavior = iota
    ByzantineEquivocating
    ByzantineFalseCommit
    ByzantineSilent
)

// Cluster methods
func (c *Cluster) SetByzantine(nodeIdx int, behavior ByzantineBehavior)
func (c *Cluster) ClearByzantine(nodeIdx int)
func (c *Cluster) IsByzantine(nodeIdx int) bool
func (c *Cluster) ByzantineCount() int

// ConsensusNode methods
func (n *ConsensusNode) SetByzantine(behavior ByzantineBehavior)
func (n *ConsensusNode) IsByzantineNode() bool
func (n *ConsensusNode) GetByzantineBehavior() ByzantineBehavior
```

#### Usage

```bash
# Run BFT scenarios
roj-elle run --scenario bft-equivocation --nodes 5 --operations 100
roj-elle run --scenario bft-minority --nodes 7 --operations 100
roj-elle run --scenario bft-threshold --nodes 7 --operations 100
roj-elle run --scenario bft-false-commit --nodes 5 --operations 100

# Via Go simulator
roj-elle sim --scenario bft-equivocation --nodes 5 --operations 100
```

#### Verification

- Equivocation is logged/detected by honest nodes
- Honest majority still reaches consensus
- Elle detects any consistency violations
- False commits are rejected by honest nodes

---

## Complete Scenario List

| Scenario | Description | Nodes | Faults |
|----------|-------------|-------|--------|
| `happy` | No faults - baseline | 3 | None |
| `partition` | Network partition | 5 | 2-3 split at op 30-70 |
| `leader-crash` | Leader crashes | 5 | Node 0 crashes at op 50 |
| `message-loss` | Message loss | 5 | 10% throughout |
| `contention` | Single key | 3 | None, high contention |
| `slow-network` | Network delays | 5 | 100-500ms delays |
| `bft-equivocation` | Byzantine equivocation | 5 | 1 node equivocates |
| `bft-minority` | Byzantine minority | 7 | 1 Byzantine (f < n/3) |
| `bft-threshold` | Byzantine at threshold | 7 | 2 Byzantine (f = n/3) |
| `bft-false-commit` | Byzantine false commit | 5 | 1 node sends false commits |

---

## Updated API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/health` | GET | Health check |
| `/api/roj/status` | GET | Cluster status |
| `/api/roj/stats` | GET | Cluster statistics |
| `/api/roj/propose` | POST | Submit proposal |
| `/api/roj/state` | GET | Get node state |
| `/api/roj/crash` | POST | Crash a node |
| `/api/roj/recover` | POST | Recover a node |
| `/api/roj/partition` | POST | Create partition |
| `/api/roj/heal` | POST | Heal partitions |
| `/api/roj/history` | GET | Get Elle history |
| `/api/roj/history/clear` | POST | Clear history |
| `/api/roj/history/stream` | WebSocket | **NEW:** Stream events |
| `/api/roj/scenario/run` | POST | Run scenario |

---

## Updated File Structure

```
ek-roj/
├── roj-core-rs/
│   ├── Cargo.toml
│   └── src/
│       ├── lib.rs
│       ├── history.rs
│       ├── consensus.rs
│       └── log.rs
├── roj-elle-harness/
│   ├── Cargo.toml              # Added tokio-tungstenite, futures-util
│   └── src/
│       ├── main.rs             # Added --metrics-file flag
│       ├── cluster.rs          # Added Byzantine detection, vote validation
│       ├── fault_injection.rs  # Added BFT scenarios, slow network
│       ├── workload.rs
│       ├── elle_runner.rs
│       ├── scenarios.rs        # Added all new scenarios
│       ├── simulator_adapter.rs # Added WebSocket streaming
│       └── metrics.rs          # NEW: Performance metrics
├── scripts/
│   ├── run-elle-tests.sh
│   └── run-elle-tests.bat
└── .github/
    └── workflows/
        └── elle-tests.yml

simulator/engine/
├── go.mod                      # Added gorilla/websocket
└── internal/roj/
    ├── consensus.go            # Added Byzantine behavior, event callbacks
    ├── cluster.go              # Added WebSocket streaming, delay injection, BFT
    └── api.go                  # Added WebSocket endpoint, BFT scenarios

web/
├── package.json               # Added d3
└── src/
    ├── App.jsx                # Added /elle route
    ├── pages/
    │   └── ElleVisualizerPage.jsx    # NEW: Main visualization page
    └── components/elle/              # NEW: Elle visualization components
        ├── HistoryUploader.jsx
        ├── TimelineView.jsx
        ├── AnomalyGraph.jsx
        ├── MonitoringDashboard.jsx
        ├── ScenarioProgress.jsx
        └── EventLog.jsx
```

---

## Dependencies

### Rust

```toml
# roj-elle-harness/Cargo.toml
tokio-tungstenite = "0.21"  # WebSocket client
futures-util = "0.3"        # Stream utilities
```

### Go

```
# simulator/engine/go.mod
github.com/gorilla/websocket v1.5.1
```

### Web

```json
// web/package.json
"d3": "^7.8.5"
```

---

## Quick Reference

```bash
# Build everything
cargo build --release -p roj-elle-harness
cd web && npm install && npm run build

# Run basic scenario
roj-elle run --scenario happy --nodes 3 --operations 100

# Run with metrics
roj-elle run --scenario happy --operations 100 --metrics-file metrics.json

# Run slow network
roj-elle run --scenario slow-network --nodes 5 --operations 50

# Run BFT scenario
roj-elle run --scenario bft-equivocation --nodes 5 --operations 100

# Start simulator and run via WebSocket
cd simulator/engine && go run ./cmd/simulator &
roj-elle sim --scenario partition --nodes 5 --operations 100

# Connect to WebSocket stream
wscat -c ws://localhost:8001/api/roj/history/stream

# Open web visualizer
cd web && npm run dev
# Navigate to http://localhost:5173/elle
```

## References

- [Elle: Inferring Isolation Anomalies from Experimental Observations](https://arxiv.org/abs/2003.10554)
- [Jepsen Elle](https://github.com/jepsen-io/elle)
- [elle-cli](https://github.com/ligurio/elle-cli) - Standalone CLI for Elle
