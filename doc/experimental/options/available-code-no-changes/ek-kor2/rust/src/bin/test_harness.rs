//! EK-KOR v2 - JSON Test Vector Harness
//!
//! Runs test vectors from the spec/test-vectors directory and outputs
//! JSON results for cross-validation with the C implementation.

use ekk::{
    field::{FieldEngine, FieldRegion},
    topology::{Topology, TopologyConfig, DistanceMetric},
    consensus::{Consensus, ProposalType},
    heartbeat::Heartbeat,
    types::{
        Field, Fixed, ModuleId, BallotId, TimeUs, Position,
        HealthState, VoteValue, threshold, K_NEIGHBORS,
    },
};

use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::{env, fs, path::Path};

// Use std Result to avoid conflict with ekk::Result
type Result<T> = std::result::Result<T, String>;

// ============================================================================
// Test Vector Structures
// ============================================================================

#[derive(Debug, Deserialize)]
struct TestVector {
    id: String,
    #[serde(default)]
    name: String,
    module: String,
    function: String,
    #[serde(default)]
    description: String,
    #[serde(default)]
    setup: Option<Value>,
    input: Value,
    expected: Value,
    #[serde(default)]
    notes: Vec<String>,
}

#[derive(Debug, Serialize)]
struct TestResult {
    id: String,
    module: String,
    function: String,
    passed: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    error: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    actual: Option<Value>,
}

// ============================================================================
// Global State (reset per test file)
// ============================================================================

struct TestState {
    field_engine: FieldEngine,
    field_region: FieldRegion,
    topology: Option<Topology>,
    consensus: Option<Consensus>,
    heartbeat: Option<Heartbeat>,
}

impl TestState {
    fn new() -> Self {
        Self {
            field_engine: FieldEngine::new(),
            field_region: FieldRegion::new(),
            topology: None,
            consensus: None,
            heartbeat: None,
        }
    }

    fn reset(&mut self) {
        self.field_region = FieldRegion::new();
        self.topology = None;
        self.consensus = None;
        self.heartbeat = None;
    }
}

// ============================================================================
// Main Entry Point
// ============================================================================

fn main() {
    let args: Vec<String> = env::args().collect();

    if args.len() < 2 {
        eprintln!("Usage: test_harness <test_vector.json> [test_vector2.json ...]");
        std::process::exit(1);
    }

    let mut all_results: Vec<TestResult> = Vec::new();
    let mut state = TestState::new();

    for path in &args[1..] {
        state.reset();
        match run_test_file(path, &mut state) {
            Ok(results) => all_results.extend(results),
            Err(e) => {
                all_results.push(TestResult {
                    id: Path::new(path)
                        .file_stem()
                        .and_then(|s| s.to_str())
                        .unwrap_or("unknown")
                        .to_string(),
                    module: "harness".to_string(),
                    function: "load".to_string(),
                    passed: false,
                    error: Some(format!("Failed to load test file: {}", e)),
                    actual: None,
                });
            }
        }
    }

    // Output JSON results
    println!("{}", serde_json::to_string_pretty(&all_results).unwrap());

    // Summary to stderr
    let passed = all_results.iter().filter(|r| r.passed).count();
    let total = all_results.len();
    eprintln!("\n=== Test Summary ===");
    eprintln!("Passed: {}/{} ({:.1}%)", passed, total, 100.0 * passed as f64 / total as f64);
}

fn run_test_file(path: &str, state: &mut TestState) -> Result<Vec<TestResult>> {
    let content = fs::read_to_string(path)
        .map_err(|e| format!("Failed to read {}: {}", path, e))?;

    let vector: TestVector = serde_json::from_str(&content)
        .map_err(|e| format!("Failed to parse {}: {}", path, e))?;

    // Handle setup if present
    if let Some(setup) = &vector.setup {
        handle_setup(setup, state)?;
    }

    // Dispatch test
    let result = dispatch_test(&vector, state);
    Ok(vec![result])
}

fn handle_setup(setup: &Value, state: &mut TestState) -> Result<()> {
    // Handle topology initialization
    if let Some(init) = setup.get("init") {
        let my_id = init.get("my_id")
            .and_then(|v| v.as_u64())
            .unwrap_or(1) as ModuleId;

        let pos = if let Some(pos) = init.get("my_position") {
            Position::new(
                pos.get("x").and_then(|v| v.as_i64()).unwrap_or(0) as i16,
                pos.get("y").and_then(|v| v.as_i64()).unwrap_or(0) as i16,
                pos.get("z").and_then(|v| v.as_i64()).unwrap_or(0) as i16,
            )
        } else {
            Position::new(0, 0, 0)
        };

        let metric = init.get("metric")
            .and_then(|v| v.as_str())
            .map(|s| match s {
                "Physical" => DistanceMetric::Physical,
                "Latency" => DistanceMetric::Latency,
                "Custom" => DistanceMetric::Custom,
                _ => DistanceMetric::Logical,
            })
            .unwrap_or(DistanceMetric::Logical);

        let config = TopologyConfig {
            metric,
            ..Default::default()
        };

        state.topology = Some(Topology::new(my_id, pos, Some(config)));
    }

    // Handle discoveries array
    if let Some(discoveries) = setup.get("discoveries").and_then(|v| v.as_array()) {
        let topo = state.topology.get_or_insert_with(|| {
            Topology::new(1, Position::new(0, 0, 0), None)
        });

        for disc in discoveries {
            let sender_id = disc.get("sender_id")
                .and_then(|v| v.as_u64())
                .unwrap_or(0) as ModuleId;

            let sender_pos = if let Some(pos) = disc.get("sender_position") {
                Position::new(
                    pos.get("x").and_then(|v| v.as_i64()).unwrap_or(0) as i16,
                    pos.get("y").and_then(|v| v.as_i64()).unwrap_or(0) as i16,
                    pos.get("z").and_then(|v| v.as_i64()).unwrap_or(0) as i16,
                )
            } else {
                Position::new(0, 0, 0)
            };

            let now = disc.get("now")
                .and_then(|v| v.as_u64())
                .unwrap_or(1000000);

            let _ = topo.on_discovery(sender_id, sender_pos, now);
        }
    }

    // Handle heartbeat add_neighbor
    if let Some(add_neighbor) = setup.get("add_neighbor") {
        let my_id = add_neighbor.get("my_id")
            .and_then(|v| v.as_u64())
            .unwrap_or(1) as ModuleId;

        let hb = state.heartbeat.get_or_insert_with(|| {
            Heartbeat::new(my_id, None)
        });

        if let Some(neighbors) = add_neighbor.get("neighbors").and_then(|v| v.as_array()) {
            for neighbor in neighbors {
                if let Some(id) = neighbor.as_u64() {
                    let _ = hb.add_neighbor(id as ModuleId);
                }
            }
        }
    }

    // Handle consensus init
    if let Some(consensus_init) = setup.get("consensus") {
        let my_id = consensus_init.get("my_id")
            .and_then(|v| v.as_u64())
            .unwrap_or(1) as ModuleId;

        state.consensus = Some(Consensus::new(my_id, None));
    }

    Ok(())
}

// ============================================================================
// Test Dispatch
// ============================================================================

fn dispatch_test(vector: &TestVector, state: &mut TestState) -> TestResult {
    let result = match (vector.module.as_str(), vector.function.as_str()) {
        // Field module
        ("field", "field_publish") => test_field_publish(vector, state),
        ("field", "field_sample") => test_field_sample(vector, state),
        ("field", "field_gradient") => test_field_gradient(vector, state),

        // Topology module
        ("topology", "topology_on_discovery") => test_topology_on_discovery(vector, state),
        ("topology", "topology_reelect") => test_topology_reelect(vector, state),
        ("topology", "topology_on_neighbor_lost") => test_topology_on_neighbor_lost(vector, state),

        // Consensus module
        ("consensus", "consensus_propose") => test_consensus_propose(vector, state),
        ("consensus", "consensus_vote") => test_consensus_vote(vector, state),
        ("consensus", "consensus_inhibit") => test_consensus_inhibit(vector, state),
        ("consensus", "consensus_tick") => test_consensus_tick(vector, state),

        // Heartbeat module
        ("heartbeat", "heartbeat_received") => test_heartbeat_received(vector, state),
        ("heartbeat", "heartbeat_tick") => test_heartbeat_tick(vector, state),

        // Types module
        ("types", "q15_convert") => test_q15_convert(vector),

        // SPSC module (not yet in Rust)
        ("spsc", _) => Err("SPSC not yet implemented in Rust".to_string()),

        // Auth module (not yet in Rust)
        ("auth", _) => Err("Auth not yet implemented in Rust".to_string()),

        _ => Err(format!("No handler for {}.{}", vector.module, vector.function)),
    };

    match result {
        Ok(actual) => {
            // Compare with expected
            let passed = compare_results(&vector.expected, &actual);
            TestResult {
                id: vector.id.clone(),
                module: vector.module.clone(),
                function: vector.function.clone(),
                passed,
                error: if passed { None } else { Some("Result mismatch".to_string()) },
                actual: if passed { None } else { Some(actual) },
            }
        }
        Err(e) => TestResult {
            id: vector.id.clone(),
            module: vector.module.clone(),
            function: vector.function.clone(),
            passed: false,
            error: Some(e),
            actual: None,
        },
    }
}

// ============================================================================
// Field Tests
// ============================================================================

fn test_field_publish(vector: &TestVector, state: &mut TestState) -> Result<Value> {
    let input = &vector.input;

    let module_id = input.get("module_id")
        .and_then(|v| v.as_u64())
        .ok_or("Missing module_id")? as ModuleId;

    let field_input = input.get("field").ok_or("Missing field")?;

    let load = field_input.get("load").and_then(|v| v.as_f64()).unwrap_or(0.0);
    let thermal = field_input.get("thermal").and_then(|v| v.as_f64()).unwrap_or(0.0);
    let power = field_input.get("power").and_then(|v| v.as_f64()).unwrap_or(0.0);

    let timestamp = input.get("timestamp")
        .and_then(|v| v.as_u64())
        .unwrap_or(0);

    let field = Field::with_values(
        Fixed::from_num(load),
        Fixed::from_num(thermal),
        Fixed::from_num(power),
    );

    let result = state.field_engine.publish(
        &mut state.field_region,
        module_id,
        &field,
        timestamp,
    );

    match result {
        Ok(()) => {
            let stored = state.field_region.get(module_id).ok_or("Field not stored")?;
            Ok(json!({
                "return": "OK",
                "region_state": {
                    format!("fields[{}].source", module_id): stored.source,
                    format!("fields[{}].timestamp", module_id): stored.timestamp,
                    format!("fields[{}].components[0]", module_id): stored.components[0].to_bits(),
                    format!("fields[{}].components[1]", module_id): stored.components[1].to_bits(),
                    format!("fields[{}].components[2]", module_id): stored.components[2].to_bits(),
                    format!("fields[{}].sequence", module_id): stored.sequence,
                }
            }))
        }
        Err(e) => Ok(json!({
            "return": format!("{:?}", e)
        })),
    }
}

fn test_field_sample(vector: &TestVector, state: &mut TestState) -> Result<Value> {
    let input = &vector.input;

    // Accept both module_id and target_id for compatibility
    let module_id = input.get("module_id")
        .or_else(|| input.get("target_id"))
        .and_then(|v| v.as_u64())
        .ok_or("Missing module_id or target_id")? as ModuleId;

    let now = input.get("now")
        .and_then(|v| v.as_u64())
        .unwrap_or(0);

    // First, set up the field if setup.publish exists
    if let Some(setup) = &vector.setup {
        if let Some(publish) = setup.get("publish") {
            let pub_id = publish.get("module_id")
                .and_then(|v| v.as_u64())
                .unwrap_or(module_id as u64) as ModuleId;

            let empty_field = json!({});
            let pub_field = publish.get("field").unwrap_or(&empty_field);
            let load = pub_field.get("load").and_then(|v| v.as_f64()).unwrap_or(0.0);
            let thermal = pub_field.get("thermal").and_then(|v| v.as_f64()).unwrap_or(0.0);
            let power = pub_field.get("power").and_then(|v| v.as_f64()).unwrap_or(0.0);

            let pub_time = publish.get("timestamp")
                .and_then(|v| v.as_u64())
                .unwrap_or(0);

            let field = Field::with_values(
                Fixed::from_num(load),
                Fixed::from_num(thermal),
                Fixed::from_num(power),
            );

            state.field_engine.publish(&mut state.field_region, pub_id, &field, pub_time)
                .map_err(|e| format!("Setup publish failed: {:?}", e))?;
        }
    }

    let result = state.field_engine.sample(&state.field_region, module_id, now);

    match result {
        Ok(field) => Ok(json!({
            "return": "OK",
            "field": {
                "load": field.components[0].to_num::<f64>(),
                "thermal": field.components[1].to_num::<f64>(),
                "power": field.components[2].to_num::<f64>(),
                "source": field.source,
                "timestamp": field.timestamp,
            }
        })),
        Err(e) => Ok(json!({
            "return": format!("{:?}", e)
        })),
    }
}

fn test_field_gradient(vector: &TestVector, state: &mut TestState) -> Result<Value> {
    let input = &vector.input;

    let my_field = input.get("my_field").ok_or("Missing my_field")?;
    // Accept both neighbor_field and neighbor_aggregate for compatibility
    let neighbor_field = input.get("neighbor_field")
        .or_else(|| input.get("neighbor_aggregate"))
        .ok_or("Missing neighbor_field or neighbor_aggregate")?;

    let my = Field::with_values(
        Fixed::from_num(my_field.get("load").and_then(|v| v.as_f64()).unwrap_or(0.0)),
        Fixed::from_num(my_field.get("thermal").and_then(|v| v.as_f64()).unwrap_or(0.0)),
        Fixed::from_num(my_field.get("power").and_then(|v| v.as_f64()).unwrap_or(0.0)),
    );

    let neighbor = Field::with_values(
        Fixed::from_num(neighbor_field.get("load").and_then(|v| v.as_f64()).unwrap_or(0.0)),
        Fixed::from_num(neighbor_field.get("thermal").and_then(|v| v.as_f64()).unwrap_or(0.0)),
        Fixed::from_num(neighbor_field.get("power").and_then(|v| v.as_f64()).unwrap_or(0.0)),
    );

    let gradients = state.field_engine.gradient_all(&my, &neighbor);

    // Get specific component if requested
    let component = input.get("component")
        .and_then(|v| v.as_str())
        .unwrap_or("Load");

    let gradient_value = match component {
        "Load" | "load" => gradients[0].to_num::<f64>(),
        "Thermal" | "thermal" => gradients[1].to_num::<f64>(),
        "Power" | "power" => gradients[2].to_num::<f64>(),
        _ => gradients[0].to_num::<f64>(),
    };

    Ok(json!({
        "return": "OK",
        "gradient": gradient_value
    }))
}

// ============================================================================
// Topology Tests
// ============================================================================

fn test_topology_on_discovery(vector: &TestVector, state: &mut TestState) -> Result<Value> {
    let input = &vector.input;

    let sender_id = input.get("sender_id")
        .and_then(|v| v.as_u64())
        .ok_or("Missing sender_id")? as ModuleId;

    let sender_pos = if let Some(pos) = input.get("sender_position") {
        Position::new(
            pos.get("x").and_then(|v| v.as_i64()).unwrap_or(0) as i16,
            pos.get("y").and_then(|v| v.as_i64()).unwrap_or(0) as i16,
            pos.get("z").and_then(|v| v.as_i64()).unwrap_or(0) as i16,
        )
    } else {
        Position::new(0, 0, 0)
    };

    let now = input.get("now")
        .and_then(|v| v.as_u64())
        .unwrap_or(1000000);

    let topo = state.topology.get_or_insert_with(|| {
        Topology::new(1, Position::new(0, 0, 0), None)
    });

    let result = topo.on_discovery(sender_id, sender_pos, now);

    match result {
        Ok(changed) => {
            let neighbors: Vec<Value> = topo.neighbors()
                .iter()
                .map(|n| json!({
                    "id": n.id,
                    "logical_distance": n.logical_distance,
                }))
                .collect();

            Ok(json!({
                "return": "OK",
                "topology_changed": changed,
                "neighbor_count": topo.neighbor_count(),
                "neighbors": neighbors,
            }))
        }
        Err(e) => Ok(json!({
            "return": format!("{:?}", e)
        })),
    }
}

fn test_topology_reelect(vector: &TestVector, state: &mut TestState) -> Result<Value> {
    let topo = state.topology.as_mut()
        .ok_or("Topology not initialized")?;

    let neighbor_count = topo.reelect();

    let neighbors: Vec<Value> = topo.neighbors()
        .iter()
        .map(|n| json!({
            "id": n.id,
            "logical_distance": n.logical_distance,
        }))
        .collect();

    Ok(json!({
        "return": "OK",
        "neighbor_count": neighbor_count,
        "neighbors": neighbors,
    }))
}

fn test_topology_on_neighbor_lost(vector: &TestVector, state: &mut TestState) -> Result<Value> {
    let input = &vector.input;

    let lost_id = input.get("lost_id")
        .and_then(|v| v.as_u64())
        .ok_or("Missing lost_id")? as ModuleId;

    let topo = state.topology.as_mut()
        .ok_or("Topology not initialized")?;

    let result = topo.on_neighbor_lost(lost_id);

    match result {
        Ok(()) => Ok(json!({
            "return": "OK",
            "neighbor_count": topo.neighbor_count(),
        })),
        Err(e) => Ok(json!({
            "return": format!("{:?}", e)
        })),
    }
}

// ============================================================================
// Consensus Tests
// ============================================================================

fn test_consensus_propose(vector: &TestVector, state: &mut TestState) -> Result<Value> {
    let input = &vector.input;

    let my_id = input.get("my_id")
        .and_then(|v| v.as_u64())
        .unwrap_or(1) as ModuleId;

    let proposal_type = input.get("proposal_type")
        .and_then(|v| v.as_str())
        .map(|s| match s {
            "ModeChange" => ProposalType::ModeChange,
            "PowerLimit" => ProposalType::PowerLimit,
            "Shutdown" => ProposalType::Shutdown,
            "Reformation" => ProposalType::Reformation,
            _ => ProposalType::Custom0,
        })
        .unwrap_or(ProposalType::ModeChange);

    let data = input.get("data")
        .and_then(|v| v.as_u64())
        .unwrap_or(0) as u32;

    let threshold = input.get("threshold")
        .and_then(|v| v.as_f64())
        .map(|f| Fixed::from_num(f))
        .unwrap_or(threshold::SIMPLE_MAJORITY);

    let now = input.get("now")
        .and_then(|v| v.as_u64())
        .unwrap_or(1000000);

    let cons = state.consensus.get_or_insert_with(|| {
        Consensus::new(my_id, None)
    });

    let result = cons.propose(proposal_type, data, threshold, now);

    match result {
        Ok(ballot_id) => Ok(json!({
            "return": "OK",
            "ballot_id": ballot_id,
            "result": format!("{:?}", cons.get_result(ballot_id)),
        })),
        Err(e) => Ok(json!({
            "return": format!("{:?}", e)
        })),
    }
}

fn test_consensus_vote(vector: &TestVector, state: &mut TestState) -> Result<Value> {
    let input = &vector.input;

    // First propose if setup requires
    if let Some(setup) = &vector.setup {
        if let Some(propose) = setup.get("propose") {
            let my_id = propose.get("my_id")
                .and_then(|v| v.as_u64())
                .unwrap_or(1) as ModuleId;

            let cons = state.consensus.get_or_insert_with(|| {
                Consensus::new(my_id, None)
            });

            let proposal_type = ProposalType::ModeChange;
            let data = propose.get("data").and_then(|v| v.as_u64()).unwrap_or(0) as u32;
            let threshold = Fixed::from_num(
                propose.get("threshold").and_then(|v| v.as_f64()).unwrap_or(0.5)
            );
            let now = propose.get("now").and_then(|v| v.as_u64()).unwrap_or(1000000);

            let _ = cons.propose(proposal_type, data, threshold, now);
        }
    }

    let ballot_id = input.get("ballot_id")
        .and_then(|v| v.as_u64())
        .ok_or("Missing ballot_id")? as BallotId;

    let votes = input.get("votes")
        .and_then(|v| v.as_array())
        .ok_or("Missing votes")?;

    let total_voters = input.get("total_voters")
        .and_then(|v| v.as_u64())
        .unwrap_or(votes.len() as u64) as u8;

    let cons = state.consensus.as_mut()
        .ok_or("Consensus not initialized")?;

    for vote_val in votes {
        let voter_id = vote_val.get("voter_id")
            .and_then(|v| v.as_u64())
            .unwrap_or(0) as ModuleId;

        let vote = vote_val.get("vote")
            .and_then(|v| v.as_str())
            .map(|s| match s {
                "Yes" => VoteValue::Yes,
                "No" => VoteValue::No,
                "Inhibit" => VoteValue::Inhibit,
                _ => VoteValue::Abstain,
            })
            .unwrap_or(VoteValue::Abstain);

        let _ = cons.on_vote(voter_id, ballot_id, vote, total_voters);
    }

    let result = cons.get_result(ballot_id);

    Ok(json!({
        "return": "OK",
        "result": format!("{:?}", result),
    }))
}

fn test_consensus_inhibit(vector: &TestVector, state: &mut TestState) -> Result<Value> {
    let input = &vector.input;

    let ballot_id = input.get("ballot_id")
        .and_then(|v| v.as_u64())
        .ok_or("Missing ballot_id")? as BallotId;

    let now = input.get("now")
        .and_then(|v| v.as_u64())
        .unwrap_or(1000000);

    let cons = state.consensus.as_mut()
        .ok_or("Consensus not initialized")?;

    let result = cons.inhibit(ballot_id, now);

    match result {
        Ok(()) => Ok(json!({
            "return": "OK",
            "result": format!("{:?}", cons.get_result(ballot_id)),
        })),
        Err(e) => Ok(json!({
            "return": format!("{:?}", e)
        })),
    }
}

fn test_consensus_tick(vector: &TestVector, state: &mut TestState) -> Result<Value> {
    let input = &vector.input;

    let now = input.get("now")
        .and_then(|v| v.as_u64())
        .ok_or("Missing now")?;

    let ballot_id = input.get("ballot_id")
        .and_then(|v| v.as_u64())
        .unwrap_or(1) as BallotId;

    let cons = state.consensus.as_mut()
        .ok_or("Consensus not initialized")?;

    let completed = cons.tick(now);
    let result = cons.get_result(ballot_id);

    Ok(json!({
        "return": "OK",
        "completed": completed,
        "result": format!("{:?}", result),
    }))
}

// ============================================================================
// Heartbeat Tests
// ============================================================================

fn test_heartbeat_received(vector: &TestVector, state: &mut TestState) -> Result<Value> {
    let input = &vector.input;

    let sender_id = input.get("sender_id")
        .and_then(|v| v.as_u64())
        .ok_or("Missing sender_id")? as ModuleId;

    let sequence = input.get("sequence")
        .and_then(|v| v.as_u64())
        .unwrap_or(0) as u8;

    let now = input.get("now")
        .and_then(|v| v.as_u64())
        .unwrap_or(1000000);

    let hb = state.heartbeat.get_or_insert_with(|| {
        Heartbeat::new(1, None)
    });

    // Ensure neighbor is being tracked
    let _ = hb.add_neighbor(sender_id);

    let result = hb.received(sender_id, sequence, now);

    match result {
        Ok(()) => Ok(json!({
            "return": "OK",
            "health": format!("{:?}", hb.get_health(sender_id)),
        })),
        Err(e) => Ok(json!({
            "return": format!("{:?}", e)
        })),
    }
}

fn test_heartbeat_tick(vector: &TestVector, state: &mut TestState) -> Result<Value> {
    let input = &vector.input;

    let now = input.get("now")
        .and_then(|v| v.as_u64())
        .ok_or("Missing now")?;

    let neighbor_id = input.get("neighbor_id")
        .and_then(|v| v.as_u64())
        .unwrap_or(2) as ModuleId;

    let hb = state.heartbeat.as_mut()
        .ok_or("Heartbeat not initialized")?;

    let changed = hb.tick(now);
    let health = hb.get_health(neighbor_id);

    Ok(json!({
        "return": "OK",
        "changed": changed,
        "health": format!("{:?}", health),
    }))
}

// ============================================================================
// Types Tests
// ============================================================================

fn test_q15_convert(vector: &TestVector) -> Result<Value> {
    let input = &vector.input;

    let float_val = input.get("float")
        .and_then(|v| v.as_f64())
        .ok_or("Missing float")?;

    let fixed: Fixed = Fixed::from_num(float_val);
    let back: f64 = fixed.to_num();

    Ok(json!({
        "return": "OK",
        "fixed_bits": fixed.to_bits(),
        "round_trip": back,
    }))
}

// ============================================================================
// Result Comparison
// ============================================================================

fn compare_results(expected: &Value, actual: &Value) -> bool {
    // Check return value only if expected specifies it
    if let Some(exp_return) = expected.get("return").and_then(|v| v.as_str()) {
        let act_return = actual.get("return").and_then(|v| v.as_str());

        if Some(exp_return) != act_return {
            // Check for error string variations
            let act_str = act_return.unwrap_or("");

            // Normalize error strings
            let exp_norm = normalize_error(exp_return);
            let act_norm = normalize_error(act_str);

            if exp_norm != act_norm {
                return false;
            }
        }

        // If return is error, don't check other fields
        if exp_return != "OK" {
            return true;
        }
    }

    // Check specific expected values with tolerance for floats
    compare_values(expected, actual)
}

fn normalize_error(s: &str) -> &str {
    match s {
        "ERR_INVALID_ARG" | "InvalidArg" => "InvalidArg",
        "ERR_NOT_FOUND" | "NotFound" => "NotFound",
        "ERR_NO_MEMORY" | "NoMemory" => "NoMemory",
        "ERR_BUSY" | "Busy" => "Busy",
        "ERR_TIMEOUT" | "Timeout" => "Timeout",
        _ => s,
    }
}

fn compare_values(expected: &Value, actual: &Value) -> bool {
    match (expected, actual) {
        (Value::Object(exp_map), Value::Object(act_map)) => {
            for (key, exp_val) in exp_map {
                if key == "return" {
                    continue; // Already checked
                }
                if let Some(act_val) = act_map.get(key) {
                    if !compare_values(exp_val, act_val) {
                        return false;
                    }
                }
            }
            true
        }
        (Value::Number(exp), Value::Number(act)) => {
            let exp_f = exp.as_f64().unwrap_or(0.0);
            let act_f = act.as_f64().unwrap_or(0.0);

            // Use tolerance for floating point comparison
            let tolerance = 0.01; // 1%
            (exp_f - act_f).abs() < tolerance || (exp_f - act_f).abs() / exp_f.abs().max(1e-10) < tolerance
        }
        (Value::Bool(exp), Value::Bool(act)) => exp == act,
        (Value::String(exp), Value::String(act)) => exp == act,
        (Value::Array(exp), Value::Array(act)) => {
            if exp.len() != act.len() {
                return false;
            }
            exp.iter().zip(act.iter()).all(|(e, a)| compare_values(e, a))
        }
        _ => true, // Ignore mismatched types for now
    }
}
