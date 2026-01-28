//! ROJ Node - Rust Implementation
//!
//! Distributed consensus node for the ROJ protocol.

use clap::Parser;
use roj_core::{Consensus, Discovery, Language, Message, Transport, Vote};
use std::io::{self, BufRead, Write};
use std::time::Duration;
use tokio::time::interval;
use tracing::{info, Level};
use tracing_subscriber::FmtSubscriber;

/// ROJ distributed consensus node
#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Args {
    /// Node name/identifier
    #[arg(short, long)]
    name: String,

    /// UDP port to listen on
    #[arg(short, long, default_value = "9990")]
    port: u16,

    /// Log level (trace, debug, info, warn, error)
    #[arg(short, long, default_value = "info")]
    log_level: String,
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
        .finish();
    tracing::subscriber::set_global_default(subscriber)?;

    info!("ROJ node \"{}\" starting (rust)", args.name);

    // Initialize discovery
    let discovery = Discovery::new(args.name.clone(), Language::Rust)?;
    let peers = discovery.peers();

    // Start mDNS
    discovery.announce()?;
    discovery.browse().await?;

    // Initialize transport
    let mut transport = Transport::new(args.port).await?;
    let local_addr = transport.local_addr()?;
    info!("Listening on {}", local_addr);
    transport.start_receive();

    // Initialize consensus
    let mut consensus = Consensus::new(args.name.clone(), peers.clone());

    // Get a clone of socket for sending
    let transport_send = Transport::new(args.port + 1000).await?;

    // Spawn heartbeat task
    let heartbeat_name = args.name.clone();
    let heartbeat_peers = peers.clone();
    tokio::spawn(async move {
        let mut ticker = interval(Duration::from_secs(1));
        loop {
            ticker.tick().await;
            let announce = Message::Announce {
                node_id: heartbeat_name.clone(),
                lang: Language::Rust,
                capabilities: vec!["consensus".to_string()],
                version: "0.1.0".to_string(),
            };

            let addrs: Vec<_> = heartbeat_peers.read().await.values().map(|p| p.addr).collect();
            if !addrs.is_empty() {
                // We can't easily share the transport here, so heartbeat is via mDNS
            }
        }
    });

    // Spawn stdin handler for proposals
    let stdin_name = args.name.clone();
    let (proposal_tx, mut proposal_rx) = tokio::sync::mpsc::channel::<(String, i64)>(16);

    std::thread::spawn(move || {
        println!("\nCommands:");
        println!("  propose <key> <value>  - Propose a consensus value");
        println!("  state                  - Show committed state");
        println!("  peers                  - Show discovered peers");
        println!("  quit                   - Exit\n");

        let stdin = io::stdin();
        for line in stdin.lock().lines() {
            if let Ok(line) = line {
                let parts: Vec<&str> = line.trim().split_whitespace().collect();
                if parts.is_empty() {
                    continue;
                }

                match parts[0] {
                    "propose" if parts.len() >= 3 => {
                        let key = parts[1].to_string();
                        if let Ok(value) = parts[2].parse::<i64>() {
                            let _ = proposal_tx.blocking_send((key, value));
                        } else {
                            println!("Invalid value (must be integer)");
                        }
                    }
                    "state" => {
                        // State is shown via consensus, need async access
                        println!("(use 'peers' to see current state)");
                    }
                    "peers" => {
                        // Can't easily access from sync context
                        println!("(check logs for peer discovery)");
                    }
                    "quit" | "exit" => {
                        std::process::exit(0);
                    }
                    _ => {
                        println!("Unknown command. Try: propose <key> <value>");
                    }
                }
            }
        }
    });

    // Main event loop
    loop {
        tokio::select! {
            // Handle incoming messages
            Some((msg, src)) = transport.recv() => {
                match msg {
                    Message::Announce { node_id, lang, .. } => {
                        // mDNS handles discovery, but we can update last_seen
                        if node_id != args.name {
                            let mut peers_lock = peers.write().await;
                            if let Some(peer) = peers_lock.get_mut(&node_id) {
                                peer.last_seen = std::time::SystemTime::now();
                            }
                        }
                    }
                    Message::Propose { proposal_id, from, key, value, timestamp } => {
                        if from != args.name {
                            let vote_msg = consensus.handle_proposal(
                                proposal_id,
                                from.clone(),
                                key,
                                value,
                                timestamp,
                            );
                            // Send vote back
                            let _ = transport_send.send(&vote_msg, src).await;
                        }
                    }
                    Message::Vote { proposal_id, from, vote } => {
                        if from != args.name {
                            if let Some(commit_msg) = consensus.handle_vote(
                                proposal_id,
                                from,
                                vote,
                            ).await {
                                // Broadcast commit
                                let addrs = consensus.peer_addresses().await;
                                let _ = transport_send.broadcast(&commit_msg, &addrs).await;
                            }
                        }
                    }
                    Message::Commit { proposal_id, key, value, voters } => {
                        consensus.handle_commit(proposal_id, key, value, voters);
                    }
                }
            }

            // Handle proposal requests from stdin
            Some((key, value)) = proposal_rx.recv() => {
                let propose_msg = consensus.create_proposal(
                    key,
                    serde_json::Value::Number(value.into()),
                );

                // Broadcast to all peers
                let addrs = consensus.peer_addresses().await;
                if addrs.is_empty() {
                    info!("No peers discovered yet");
                } else {
                    let _ = transport_send.broadcast(&propose_msg, &addrs).await;
                }
            }
        }
    }
}
