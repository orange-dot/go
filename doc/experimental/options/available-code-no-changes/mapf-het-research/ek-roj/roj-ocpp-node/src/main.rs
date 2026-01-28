//! ROJ-OCPP Node - CLI for OCPP adapter
//!
//! Demonstrates OCPP 2.0.1 to ROJ federation.
//!
//! # Usage
//!
//! ```bash
//! # Start with defaults
//! roj-ocpp-node --name alpha --station CS001
//!
//! # Connect to specific CSMS
//! roj-ocpp-node --name alpha --station CS001 \
//!     --ocpp-url ws://localhost:8180/steve/websocket/CentralSystemService
//!
//! # Custom ROJ port
//! roj-ocpp-node --name alpha --station CS001 --roj-port 9991
//! ```
//!
//! # Demo Scenario
//!
//! 1. Start multiple nodes (alpha, beta, gamma)
//! 2. CSMS sends SetChargingProfile to any node
//! 3. All nodes reach consensus on power limit
//! 4. All nodes report consistent state to CSMS

use clap::Parser;
use roj_adapter_ocpp::{Adapter, AdapterConfig};
use tracing::{info, Level};
use tracing_subscriber::FmtSubscriber;

/// ROJ-OCPP distributed charging point adapter
#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Args {
    /// Node name/identifier
    #[arg(short, long)]
    name: String,

    /// OCPP station ID
    #[arg(short, long, default_value = "EK3-001")]
    station: String,

    /// OCPP CSMS WebSocket URL
    #[arg(long, default_value = "ws://localhost:8180/steve/websocket/CentralSystemService")]
    ocpp_url: String,

    /// ROJ UDP port
    #[arg(long, default_value = "9990")]
    roj_port: u16,

    /// Number of EVSEs
    #[arg(long, default_value = "1")]
    evse_count: u32,

    /// Vendor name
    #[arg(long, default_value = "Elektrokombinacija")]
    vendor: String,

    /// Model name
    #[arg(long, default_value = "EK3-OCPP")]
    model: String,

    /// Log level (trace, debug, info, warn, error)
    #[arg(short, long, default_value = "info")]
    log_level: String,

    /// Disable mDNS discovery (use static peers only)
    #[arg(long)]
    no_mdns: bool,

    /// Static peer addresses (can be repeated)
    #[arg(long)]
    peer: Vec<String>,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args = Args::parse();

    // Setup logging
    let level = match args.log_level.as_str() {
        "trace" => Level::TRACE,
        "debug" => Level::DEBUG,
        "info" => Level::INFO,
        "warn" => Level::WARN,
        "error" => Level::ERROR,
        _ => Level::INFO,
    };

    let subscriber = FmtSubscriber::builder()
        .with_max_level(level)
        .with_target(false)
        .with_thread_ids(false)
        .finish();
    tracing::subscriber::set_global_default(subscriber)?;

    // Print banner
    println!();
    println!("╔══════════════════════════════════════════════════════════════╗");
    println!("║           ROJ-OCPP Node - Distributed Charging Point         ║");
    println!("╠══════════════════════════════════════════════════════════════╣");
    println!("║  Node:     {:<50} ║", args.name);
    println!("║  Station:  {:<50} ║", args.station);
    println!("║  OCPP URL: {:<50} ║", truncate(&args.ocpp_url, 50));
    println!("║  ROJ Port: {:<50} ║", args.roj_port);
    println!("║  EVSEs:    {:<50} ║", args.evse_count);
    println!("╚══════════════════════════════════════════════════════════════╝");
    println!();

    // Build configuration
    let mut config = AdapterConfig::new(&args.name, &args.station, &args.ocpp_url)
        .with_vendor(&args.vendor, &args.model)
        .with_evse_count(args.evse_count)
        .with_roj_port(args.roj_port);

    if args.no_mdns {
        config = config.without_mdns();
    }

    // Add static peers
    for peer_str in &args.peer {
        if let Ok(addr) = peer_str.parse() {
            config = config.with_peer(addr);
            info!("Added static peer: {}", addr);
        } else {
            eprintln!("Invalid peer address: {}", peer_str);
        }
    }

    info!("Starting ROJ-OCPP adapter...");

    // Create and run adapter
    let adapter = Adapter::new(config).await?;
    adapter.run().await?;

    Ok(())
}

/// Truncate string with ellipsis
fn truncate(s: &str, max_len: usize) -> String {
    if s.len() <= max_len {
        s.to_string()
    } else {
        format!("{}...", &s[..max_len - 3])
    }
}
