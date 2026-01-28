//! ROJ Elle Harness - Consistency testing for ROJ consensus
//!
//! This CLI tool provides Elle-based consistency verification for ROJ:
//! - `run` - Run a test scenario and collect history (in-memory cluster)
//! - `sim` - Run a test scenario using the Go simulator (CAN bus)
//! - `check` - Check a history file with Elle
//! - `suite` - Run the full test suite

mod cluster;
mod fault_injection;
mod metrics;
mod workload;
mod elle_runner;
mod scenarios;
mod simulator_adapter;

use clap::{Parser, Subcommand};
use std::path::PathBuf;
use tracing::info;
use tracing_subscriber::{EnvFilter, fmt};

#[derive(Parser)]
#[command(name = "roj-elle")]
#[command(about = "Elle consistency testing harness for ROJ consensus")]
#[command(version)]
struct Cli {
    #[command(subcommand)]
    command: Commands,

    /// Enable verbose logging
    #[arg(short, long, global = true)]
    verbose: bool,
}

#[derive(Subcommand)]
enum Commands {
    /// Run a test scenario (in-memory cluster)
    Run {
        /// Scenario to run
        #[arg(short, long, default_value = "happy")]
        scenario: String,

        /// Number of nodes in the cluster
        #[arg(short, long, default_value = "3")]
        nodes: usize,

        /// Number of operations to execute
        #[arg(short, long, default_value = "100")]
        operations: usize,

        /// Output directory for results
        #[arg(short = 'd', long, default_value = "./results")]
        output_dir: PathBuf,

        /// Path to write performance metrics JSON
        #[arg(long)]
        metrics_file: Option<PathBuf>,
    },

    /// Run a test scenario using Go simulator (CAN bus)
    Sim {
        /// Scenario to run
        #[arg(short, long, default_value = "happy")]
        scenario: String,

        /// Number of nodes in the cluster
        #[arg(short, long, default_value = "5")]
        nodes: usize,

        /// Number of operations to execute
        #[arg(short, long, default_value = "100")]
        operations: usize,

        /// Output directory for results
        #[arg(short = 'd', long, default_value = "./results")]
        output_dir: PathBuf,

        /// Simulator API URL
        #[arg(long, default_value = "http://localhost:8001")]
        simulator_url: String,

        /// Path to elle-cli JAR
        #[arg(long)]
        elle_jar: Option<PathBuf>,

        /// Wait timeout for simulator connection (seconds)
        #[arg(long, default_value = "10")]
        timeout: u64,
    },

    /// Check a history file with Elle
    Check {
        /// Path to history JSON file
        #[arg(short, long)]
        history: PathBuf,

        /// Elle consistency model to check
        #[arg(short, long, default_value = "list-append")]
        model: String,

        /// Path to elle-cli JAR
        #[arg(long)]
        elle_jar: Option<PathBuf>,
    },

    /// Run the full test suite
    Suite {
        /// Output directory for results
        #[arg(short, long, default_value = "./results")]
        output_dir: PathBuf,

        /// Output JUnit XML file
        #[arg(long)]
        junit_xml: Option<PathBuf>,

        /// Path to elle-cli JAR
        #[arg(long)]
        elle_jar: Option<PathBuf>,
    },

    /// Download elle-cli JAR
    Download {
        /// Target directory
        #[arg(short, long)]
        target: Option<PathBuf>,
    },
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let cli = Cli::parse();

    // Initialize logging
    let filter = if cli.verbose {
        EnvFilter::new("debug")
    } else {
        EnvFilter::new("info")
    };

    fmt()
        .with_env_filter(filter)
        .with_target(false)
        .init();

    match cli.command {
        Commands::Run {
            scenario,
            nodes,
            operations,
            output_dir,
            metrics_file,
        } => {
            info!("Running scenario: {} with {} nodes, {} operations", scenario, nodes, operations);

            let scenario_config = scenarios::get_scenario(&scenario)?;
            let results = scenarios::run_scenario_with_metrics(scenario_config, nodes, operations).await?;

            // Ensure output directory exists
            std::fs::create_dir_all(&output_dir)?;

            // Write history
            let history_path = output_dir.join(format!("{}-history.json", scenario));
            std::fs::write(&history_path, &results.history_json)?;
            info!("History written to: {}", history_path.display());

            // Write summary
            let summary_path = output_dir.join(format!("{}-summary.json", scenario));
            let summary = serde_json::json!({
                "scenario": scenario,
                "nodes": nodes,
                "operations": operations,
                "events": results.event_count,
                "commits": results.commit_count,
                "failures": results.failure_count,
                "duration_ms": results.duration_ms,
            });
            std::fs::write(&summary_path, serde_json::to_string_pretty(&summary)?)?;
            info!("Summary written to: {}", summary_path.display());

            // Write metrics if requested
            if let Some(ref metrics_path) = metrics_file {
                if let Some(ref metrics) = results.metrics {
                    let metrics_json = metrics.export_json(&scenario);
                    std::fs::write(metrics_path, &metrics_json)?;
                    info!("Metrics written to: {}", metrics_path.display());

                    // Print summary to console
                    let summary = metrics.summary();
                    println!("\nPerformance Metrics:");
                    println!("  Total operations: {}", summary.total_ops);
                    println!("  Latency (min/avg/max): {}/{:.0}/{} us",
                        summary.min_latency_us, summary.avg_latency_us, summary.max_latency_us);
                    println!("  Latency p50/p95/p99: {}/{}/{} us",
                        summary.p50_latency_us, summary.p95_latency_us, summary.p99_latency_us);
                    println!("  Throughput: {:.1} ops/sec", summary.avg_throughput_ops_sec);
                }
            }
        }

        Commands::Sim {
            scenario,
            nodes,
            operations,
            output_dir,
            simulator_url,
            elle_jar,
            timeout,
        } => {
            info!("Running simulator scenario: {} with {} nodes, {} operations", scenario, nodes, operations);
            info!("Connecting to simulator at: {}", simulator_url);

            let adapter = simulator_adapter::SimulatorAdapter::new(&simulator_url);

            // Wait for simulator to be ready
            adapter.wait_for_ready(std::time::Duration::from_secs(timeout)).await
                .map_err(|e| format!("Simulator not ready: {}", e))?;

            // Resolve elle jar path
            let resolved_jar = elle_runner::resolve_elle_jar(None).ok();
            let jar_path: Option<PathBuf> = elle_jar.clone().and_then(|p| {
                if p.exists() { Some(p) } else { None }
            }).or(resolved_jar);
            let jar_path_ref = jar_path.as_ref().map(|p| p.as_path());

            // Run scenario
            let results = simulator_adapter::run_simulator_scenario(
                &adapter,
                &scenario,
                nodes,
                operations,
                jar_path_ref,
            ).await.map_err(|e| format!("Scenario failed: {}", e))?;

            // Ensure output directory exists
            std::fs::create_dir_all(&output_dir)?;

            // Write history
            let history_path = output_dir.join(format!("sim-{}-history.json", scenario));
            std::fs::write(&history_path, &results.history_json)?;
            info!("History written to: {}", history_path.display());

            // Write summary
            let summary_path = output_dir.join(format!("sim-{}-summary.json", scenario));
            let summary = serde_json::json!({
                "scenario": scenario,
                "source": "simulator",
                "simulator_url": simulator_url,
                "nodes": nodes,
                "operations": operations,
                "events": results.event_count,
                "duration_ms": results.duration_ms,
                "elle_valid": results.elle_result.as_ref().map(|r| r.valid),
                "elle_anomaly": results.elle_result.as_ref().and_then(|r| r.anomaly.clone()),
            });
            std::fs::write(&summary_path, serde_json::to_string_pretty(&summary)?)?;
            info!("Summary written to: {}", summary_path.display());

            // Print result
            if let Some(ref elle_result) = results.elle_result {
                println!("\nElle Analysis Result:");
                println!("  Valid: {}", elle_result.valid);
                if let Some(ref anomaly) = elle_result.anomaly {
                    println!("  Anomaly: {}", anomaly);
                }
                if !elle_result.valid {
                    std::process::exit(1);
                }
            }
        }

        Commands::Check {
            history,
            model,
            elle_jar,
        } => {
            info!("Checking history: {} with model: {}", history.display(), model);

            let jar_path = elle_runner::resolve_elle_jar(elle_jar)?;
            let result = elle_runner::check_history(&jar_path, &history, &model)?;

            println!("\nElle Analysis Result:");
            println!("  Valid: {}", result.valid);
            if let Some(ref anomaly) = result.anomaly {
                println!("  Anomaly: {}", anomaly);
            }
            if !result.valid {
                std::process::exit(1);
            }
        }

        Commands::Suite {
            output_dir,
            junit_xml,
            elle_jar,
        } => {
            info!("Running full test suite");

            let jar_path = elle_runner::resolve_elle_jar(elle_jar)?;
            let results = scenarios::run_suite(&output_dir, &jar_path).await?;

            // Print summary
            println!("\nTest Suite Results:");
            println!("  Total: {}", results.total);
            println!("  Passed: {}", results.passed);
            println!("  Failed: {}", results.failed);

            // Write JUnit XML if requested
            if let Some(xml_path) = junit_xml {
                let xml = results.to_junit_xml();
                std::fs::write(&xml_path, xml)?;
                info!("JUnit XML written to: {}", xml_path.display());
            }

            if results.failed > 0 {
                std::process::exit(1);
            }
        }

        Commands::Download { target } => {
            let target_dir = target.unwrap_or_else(|| {
                dirs::home_dir()
                    .unwrap_or_else(|| PathBuf::from("."))
                    .join(".elle-cli")
            });

            info!("Downloading elle-cli to: {}", target_dir.display());
            elle_runner::download_elle_cli(&target_dir)?;
            info!("Download complete");
        }
    }

    Ok(())
}

/// Helper to get home directory
mod dirs {
    use std::path::PathBuf;

    pub fn home_dir() -> Option<PathBuf> {
        #[cfg(windows)]
        {
            std::env::var("USERPROFILE").ok().map(PathBuf::from)
        }
        #[cfg(not(windows))]
        {
            std::env::var("HOME").ok().map(PathBuf::from)
        }
    }
}
