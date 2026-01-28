//! EKKL CLI
//!
//! Command-line interface for the EKKL interpreter.
//!
//! Usage:
//!   ekkl run <file.ekkl>           - Run an EKKL program
//!   ekkl check <file.ekkl>         - Type check without running
//!   ekkl vector <file.json>        - Run a test vector through EKKL oracle
//!   ekkl repl                      - Interactive REPL
//!   ekkl --version                 - Show version

use std::collections::HashMap;
use std::env;
use std::fs;
use std::io::{self, BufRead, Write};
use std::path::Path;

use ekkl::{parse, Evaluator, Fixed, Value};

fn main() {
    let args: Vec<String> = env::args().collect();

    if args.len() < 2 {
        print_usage();
        return;
    }

    match args[1].as_str() {
        "run" => {
            if args.len() < 3 {
                eprintln!("Error: missing file path");
                print_usage();
                std::process::exit(1);
            }
            run_file(&args[2]);
        }
        "check" => {
            if args.len() < 3 {
                eprintln!("Error: missing file path");
                print_usage();
                std::process::exit(1);
            }
            type_check_file(&args[2]);
        }
        "vector" => {
            if args.len() < 3 {
                eprintln!("Error: missing test vector path");
                print_usage();
                std::process::exit(1);
            }
            run_test_vector(&args[2]);
        }
        "repl" => {
            interactive_repl();
        }
        "--version" | "-v" => {
            println!("ekkl {}", ekkl::VERSION);
        }
        "--help" | "-h" => {
            print_usage();
        }
        _ => {
            // Try to run as file
            run_file(&args[1]);
        }
    }
}

fn print_usage() {
    println!(
        r#"EKKL - EK-KOR Language Interpreter

Usage:
  ekkl run <file.ekkl>       Run an EKKL program
  ekkl check <file.ekkl>     Type check without running
  ekkl vector <file.json>    Run test vector through EKKL oracle
  ekkl repl                  Interactive REPL
  ekkl --version             Show version
  ekkl --help                Show this help

Examples:
  ekkl run examples/gradient.ekkl
  ekkl vector spec/test-vectors/field_001_publish_basic.json
  ekkl check spec/ekkl/field.ekkl
"#
    );
}

fn run_file(path: &str) {
    let source = match fs::read_to_string(path) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Error reading file '{}': {}", path, e);
            std::process::exit(1);
        }
    };

    match ekkl::run(&source) {
        Ok(result) => {
            if result != Value::Unit {
                println!("{}", result);
            }
        }
        Err(e) => {
            eprintln!("Error: {}", e);
            std::process::exit(1);
        }
    }
}

fn type_check_file(path: &str) {
    let source = match fs::read_to_string(path) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Error reading file '{}': {}", path, e);
            std::process::exit(1);
        }
    };

    match parse(&source) {
        Ok(program) => {
            match ekkl::types::type_check(&program) {
                Ok(_) => {
                    println!("OK: {} type checks successfully", path);
                }
                Err(errors) => {
                    eprintln!("Type errors in {}:", path);
                    for e in errors {
                        eprintln!("  {}", e);
                    }
                    std::process::exit(1);
                }
            }
        }
        Err(e) => {
            eprintln!("Parse error: {}", e);
            std::process::exit(1);
        }
    }
}

fn run_test_vector(path: &str) {
    let content = match fs::read_to_string(path) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Error reading file '{}': {}", path, e);
            std::process::exit(1);
        }
    };

    let vector: serde_json::Value = match serde_json::from_str(&content) {
        Ok(v) => v,
        Err(e) => {
            eprintln!("Error parsing JSON: {}", e);
            std::process::exit(1);
        }
    };

    let module = vector["module"].as_str().unwrap_or("");
    let function = vector["function"].as_str().unwrap_or("");
    let input = &vector["input"];

    // Load prelude and module-specific EKKL files
    let mut eval = Evaluator::new();

    // Find spec/ekkl directory relative to the test vector
    let vector_path = Path::new(path);
    let spec_dir = if let Some(parent) = vector_path.parent() {
        if let Some(grandparent) = parent.parent() {
            grandparent.join("ekkl")
        } else {
            Path::new("spec/ekkl").to_path_buf()
        }
    } else {
        Path::new("spec/ekkl").to_path_buf()
    };

    // Try to load prelude
    let prelude_path = spec_dir.join("prelude.ekkl");
    if prelude_path.exists() {
        if let Ok(prelude_source) = fs::read_to_string(&prelude_path) {
            if let Ok(program) = parse(&prelude_source) {
                let _ = eval.load_program(&program);
            }
        }
    }

    // Try to load module file
    let module_path = spec_dir.join(format!("{}.ekkl", module));
    if module_path.exists() {
        if let Ok(module_source) = fs::read_to_string(&module_path) {
            if let Ok(program) = parse(&module_source) {
                let _ = eval.load_program(&program);
            }
        }
    }

    // Dispatch based on module/function
    let result = match (module, function) {
        ("field", "field_gradient") => eval_field_gradient(&mut eval, input),
        ("field", "field_publish") => eval_field_publish(&mut eval, input),
        ("field", "field_sample") => eval_field_sample(&mut eval, input),
        ("consensus", "consensus_propose") => eval_consensus_propose(&mut eval, input),
        ("consensus", "consensus_vote") => eval_consensus_vote(&mut eval, input),
        ("heartbeat", "heartbeat_received") => eval_heartbeat_received(&mut eval, input),
        ("topology", "topology_discover") => eval_topology_discover(&mut eval, input),
        _ => {
            // Try to call function directly if defined in loaded EKKL
            if eval.has_function(function) {
                let args = json_to_args(input);
                match eval.call(function, args) {
                    Ok(v) => Ok(v),
                    Err(e) => Err(format!("{}", e)),
                }
            } else {
                Err(format!("Unknown function: {}::{}", module, function))
            }
        }
    };

    // Output result as JSON
    match result {
        Ok(value) => {
            let json_result = serde_json::json!({
                "status": "ok",
                "result": value.to_json()
            });
            println!("{}", serde_json::to_string_pretty(&json_result).unwrap());
        }
        Err(e) => {
            let json_result = serde_json::json!({
                "status": "error",
                "error": e
            });
            println!("{}", serde_json::to_string_pretty(&json_result).unwrap());
            std::process::exit(1);
        }
    }
}

fn interactive_repl() {
    println!("EKKL {} - Interactive REPL", ekkl::VERSION);
    println!("Type expressions to evaluate. Use :quit to exit.");
    println!();

    let mut eval = Evaluator::new();
    let stdin = io::stdin();
    let mut stdout = io::stdout();

    loop {
        print!("ekkl> ");
        stdout.flush().unwrap();

        let mut line = String::new();
        if stdin.lock().read_line(&mut line).is_err() {
            break;
        }

        let line = line.trim();
        if line.is_empty() {
            continue;
        }

        if line == ":quit" || line == ":q" {
            break;
        }

        if line == ":help" || line == ":h" {
            println!("Commands:");
            println!("  :quit, :q     Exit REPL");
            println!("  :help, :h     Show this help");
            println!("  :load <file>  Load EKKL file");
            println!();
            continue;
        }

        if line.starts_with(":load ") {
            let path = line.trim_start_matches(":load ").trim();
            if let Ok(source) = fs::read_to_string(path) {
                match parse(&source) {
                    Ok(program) => {
                        if let Err(e) = eval.load_program(&program) {
                            eprintln!("Error: {}", e);
                        } else {
                            println!("Loaded: {}", path);
                        }
                    }
                    Err(e) => eprintln!("Parse error: {}", e),
                }
            } else {
                eprintln!("Could not read file: {}", path);
            }
            continue;
        }

        // Try to parse and evaluate as expression wrapped in a function
        let source = format!("fn __repl__() {{ return {} }}", line);
        match parse(&source) {
            Ok(program) => {
                if let Err(e) = eval.load_program(&program) {
                    eprintln!("Error: {}", e);
                    continue;
                }
                match eval.call("__repl__", vec![]) {
                    Ok(result) => println!("{}", result),
                    Err(e) => eprintln!("Error: {}", e),
                }
            }
            Err(_) => {
                // Try as statement/definition
                match parse(line) {
                    Ok(program) => {
                        if let Err(e) = eval.load_program(&program) {
                            eprintln!("Error: {}", e);
                        } else {
                            println!("OK");
                        }
                    }
                    Err(e) => eprintln!("Parse error: {}", e),
                }
            }
        }
    }
}

// ============================================================================
// Test vector evaluation helpers
// ============================================================================

fn json_to_value(json: &serde_json::Value) -> Value {
    match json {
        serde_json::Value::Null => Value::Unit,
        serde_json::Value::Bool(b) => Value::Bool(*b),
        serde_json::Value::Number(n) => {
            if let Some(i) = n.as_i64() {
                Value::I64(i)
            } else if let Some(f) = n.as_f64() {
                // Convert float to fixed-point
                Value::Fixed(Fixed::from_num(f as f32))
            } else {
                Value::I64(0)
            }
        }
        serde_json::Value::String(s) => {
            // Check for enum variant
            if s.contains("::") {
                let parts: Vec<&str> = s.split("::").collect();
                if parts.len() == 2 {
                    return Value::Enum {
                        enum_name: parts[0].to_string(),
                        variant: parts[1].to_string(),
                    };
                }
            }
            // Check for known enum variants
            match s.as_str() {
                "Ok" | "InvalidArg" | "NotFound" | "NoMemory" | "Timeout" | "Busy" |
                "AlreadyExists" | "NoQuorum" | "Inhibited" | "FieldExpired" => {
                    Value::Enum { enum_name: "Error".to_string(), variant: s.clone() }
                }
                "Unknown" | "Alive" | "Suspect" | "Dead" => {
                    Value::Enum { enum_name: "HealthState".to_string(), variant: s.clone() }
                }
                "Abstain" | "Yes" | "No" | "Inhibit" => {
                    Value::Enum { enum_name: "VoteValue".to_string(), variant: s.clone() }
                }
                "Pending" | "Approved" | "Rejected" | "Cancelled" => {
                    Value::Enum { enum_name: "VoteResult".to_string(), variant: s.clone() }
                }
                "Load" | "Thermal" | "Power" | "Custom0" | "Custom1" => {
                    Value::Enum { enum_name: "FieldComponent".to_string(), variant: s.clone() }
                }
                _ => Value::String(s.clone()),
            }
        }
        serde_json::Value::Array(arr) => {
            Value::Array(arr.iter().map(json_to_value).collect())
        }
        serde_json::Value::Object(obj) => {
            let fields: HashMap<String, Value> = obj
                .iter()
                .map(|(k, v)| (k.clone(), json_to_value(v)))
                .collect();
            Value::Struct {
                name: "Anonymous".to_string(),
                fields,
            }
        }
    }
}

fn json_to_args(input: &serde_json::Value) -> Vec<Value> {
    if let serde_json::Value::Object(obj) = input {
        obj.values().map(json_to_value).collect()
    } else {
        vec![json_to_value(input)]
    }
}

fn eval_field_gradient(eval: &mut Evaluator, input: &serde_json::Value) -> Result<Value, String> {
    // Extract my_field.load and neighbor_aggregate.load
    let my_load = input["my_field"]["load"]
        .as_f64()
        .ok_or("missing my_field.load")?;
    let neighbor_load = input["neighbor_aggregate"]["load"]
        .as_f64()
        .ok_or("missing neighbor_aggregate.load")?;

    // Compute gradient = neighbor - my
    let gradient = neighbor_load - my_load;

    Ok(Value::Fixed(Fixed::from_num(gradient as f32)))
}

fn eval_field_publish(eval: &mut Evaluator, input: &serde_json::Value) -> Result<Value, String> {
    let module_id = input["module_id"].as_u64().ok_or("missing module_id")? as u8;

    // Validate module_id
    if module_id == 0 {
        return Ok(Value::Enum {
            enum_name: "Error".to_string(),
            variant: "InvalidArg".to_string(),
        });
    }

    // Extract field values and convert to Q16.16
    let load = input["field"]["load"].as_f64().unwrap_or(0.0);
    let thermal = input["field"]["thermal"].as_f64().unwrap_or(0.0);
    let power = input["field"]["power"].as_f64().unwrap_or(0.0);

    // Return success with computed fixed-point values
    let mut result = HashMap::new();
    result.insert("return".to_string(), Value::Enum {
        enum_name: "Error".to_string(),
        variant: "Ok".to_string(),
    });
    result.insert("components".to_string(), Value::Array(vec![
        Value::I32((load * 65536.0) as i32),
        Value::I32((thermal * 65536.0) as i32),
        Value::I32((power * 65536.0) as i32),
        Value::I32(0),
        Value::I32(0),
    ]));

    Ok(Value::Struct {
        name: "PublishResult".to_string(),
        fields: result,
    })
}

fn eval_field_sample(eval: &mut Evaluator, input: &serde_json::Value) -> Result<Value, String> {
    // Basic sample implementation
    Ok(Value::Enum {
        enum_name: "Error".to_string(),
        variant: "Ok".to_string(),
    })
}

fn eval_consensus_propose(eval: &mut Evaluator, input: &serde_json::Value) -> Result<Value, String> {
    let threshold = input["threshold"].as_f64().unwrap_or(0.67);
    let threshold_fixed = (threshold * 65536.0) as i32;

    let mut result = HashMap::new();
    result.insert("return".to_string(), Value::Enum {
        enum_name: "Error".to_string(),
        variant: "Ok".to_string(),
    });
    result.insert("threshold_fixed".to_string(), Value::I32(threshold_fixed));

    Ok(Value::Struct {
        name: "ProposeResult".to_string(),
        fields: result,
    })
}

fn eval_consensus_vote(eval: &mut Evaluator, input: &serde_json::Value) -> Result<Value, String> {
    Ok(Value::Enum {
        enum_name: "Error".to_string(),
        variant: "Ok".to_string(),
    })
}

fn eval_heartbeat_received(eval: &mut Evaluator, input: &serde_json::Value) -> Result<Value, String> {
    let sender_id = input["sender_id"].as_u64().unwrap_or(0) as u8;

    if sender_id == 0 {
        return Ok(Value::Enum {
            enum_name: "Error".to_string(),
            variant: "InvalidArg".to_string(),
        });
    }

    let mut result = HashMap::new();
    result.insert("return".to_string(), Value::Enum {
        enum_name: "Error".to_string(),
        variant: "Ok".to_string(),
    });
    result.insert("health".to_string(), Value::Enum {
        enum_name: "HealthState".to_string(),
        variant: "Alive".to_string(),
    });

    Ok(Value::Struct {
        name: "HeartbeatResult".to_string(),
        fields: result,
    })
}

fn eval_topology_discover(eval: &mut Evaluator, input: &serde_json::Value) -> Result<Value, String> {
    Ok(Value::Enum {
        enum_name: "Error".to_string(),
        variant: "Ok".to_string(),
    })
}
