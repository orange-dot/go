//! Elle CLI runner
//!
//! Spawns elle-cli JAR to analyze history files.

use std::path::{Path, PathBuf};
use std::process::Command;
use thiserror::Error;
use tracing::{debug, info, warn};

/// Elle CLI download URL (elle-cli releases)
const ELLE_CLI_URL: &str = "https://github.com/ligurio/elle-cli/releases/download/0.1.7/elle-cli-0.1.7-standalone.jar";
const ELLE_CLI_JAR_NAME: &str = "elle-cli.jar";

/// Errors from Elle runner
#[derive(Error, Debug)]
pub enum ElleError {
    #[error("elle-cli JAR not found at {0}")]
    JarNotFound(PathBuf),

    #[error("Java runtime not found")]
    JavaNotFound,

    #[error("Failed to run elle-cli: {0}")]
    ExecutionFailed(String),

    #[error("Failed to parse elle output: {0}")]
    ParseError(String),

    #[error("Download failed: {0}")]
    DownloadFailed(String),

    #[error("IO error: {0}")]
    IoError(#[from] std::io::Error),
}

/// Result of Elle analysis
#[derive(Debug, Clone)]
pub struct ElleResult {
    /// Whether the history is valid (no anomalies)
    pub valid: bool,
    /// Type of anomaly detected (if any)
    pub anomaly: Option<String>,
    /// Raw output from Elle
    pub raw_output: String,
    /// Graphs generated (if any)
    pub graphs: Vec<PathBuf>,
}

/// Resolve the path to elle-cli JAR
pub fn resolve_elle_jar(explicit_path: Option<PathBuf>) -> Result<PathBuf, ElleError> {
    // Check explicit path first
    if let Some(path) = explicit_path {
        if path.exists() {
            return Ok(path);
        }
        return Err(ElleError::JarNotFound(path));
    }

    // Check ~/.elle-cli/elle-cli.jar
    let home_path = dirs_home()
        .map(|h| h.join(".elle-cli").join(ELLE_CLI_JAR_NAME));

    if let Some(ref path) = home_path {
        if path.exists() {
            return Ok(path.clone());
        }
    }

    // Check current directory
    let local_path = PathBuf::from(ELLE_CLI_JAR_NAME);
    if local_path.exists() {
        return Ok(local_path);
    }

    // Check ELLE_CLI_JAR environment variable
    if let Ok(env_path) = std::env::var("ELLE_CLI_JAR") {
        let path = PathBuf::from(env_path);
        if path.exists() {
            return Ok(path);
        }
    }

    Err(ElleError::JarNotFound(
        home_path.unwrap_or_else(|| PathBuf::from("~/.elle-cli/elle-cli.jar")),
    ))
}

/// Check if Java runtime is available
pub fn check_java() -> Result<String, ElleError> {
    let output = Command::new("java")
        .arg("-version")
        .output()
        .map_err(|_| ElleError::JavaNotFound)?;

    if output.status.success() {
        // Java version is in stderr (weird but true)
        let version = String::from_utf8_lossy(&output.stderr);
        Ok(version.lines().next().unwrap_or("unknown").to_string())
    } else {
        Err(ElleError::JavaNotFound)
    }
}

/// Run Elle on a history file
pub fn check_history(
    jar_path: &Path,
    history_path: &Path,
    model: &str,
) -> Result<ElleResult, ElleError> {
    // Verify JAR exists
    if !jar_path.exists() {
        return Err(ElleError::JarNotFound(jar_path.to_path_buf()));
    }

    // Verify history file exists
    if !history_path.exists() {
        return Err(ElleError::ExecutionFailed(format!(
            "History file not found: {}",
            history_path.display()
        )));
    }

    // Check Java
    let java_version = check_java()?;
    debug!("Using Java: {}", java_version);

    // Build command
    let mut cmd = Command::new("java");
    cmd.arg("-jar")
        .arg(jar_path)
        .arg("--model")
        .arg(model)
        .arg(history_path);

    info!("Running: java -jar {} --model {} {}",
        jar_path.display(), model, history_path.display());

    // Execute
    let output = cmd.output()?;
    let stdout = String::from_utf8_lossy(&output.stdout).to_string();
    let stderr = String::from_utf8_lossy(&output.stderr).to_string();

    debug!("Elle stdout: {}", stdout);
    if !stderr.is_empty() {
        debug!("Elle stderr: {}", stderr);
    }

    // Parse output
    let combined = format!("{}\n{}", stdout, stderr);
    parse_elle_output(&combined, output.status.success())
}

/// Parse Elle CLI output
fn parse_elle_output(output: &str, success: bool) -> Result<ElleResult, ElleError> {
    let output_lower = output.to_lowercase();

    // Check for validity
    let valid = success
        && !output_lower.contains("anomaly")
        && !output_lower.contains("inconsistent")
        && !output_lower.contains("invalid");

    // Extract anomaly type if present
    let anomaly = if output_lower.contains("g0") {
        Some("G0 (write-write conflict)".to_string())
    } else if output_lower.contains("g1a") {
        Some("G1a (aborted read)".to_string())
    } else if output_lower.contains("g1b") {
        Some("G1b (intermediate read)".to_string())
    } else if output_lower.contains("g1c") {
        Some("G1c (circular information flow)".to_string())
    } else if output_lower.contains("g2") {
        Some("G2 (anti-dependency cycle)".to_string())
    } else if output_lower.contains("internal") {
        Some("Internal consistency violation".to_string())
    } else if output_lower.contains("lost-update") || output_lower.contains("lost update") {
        Some("Lost update".to_string())
    } else if output_lower.contains("dirty-read") || output_lower.contains("dirty read") {
        Some("Dirty read".to_string())
    } else if !valid {
        Some("Unknown anomaly".to_string())
    } else {
        None
    };

    Ok(ElleResult {
        valid,
        anomaly,
        raw_output: output.to_string(),
        graphs: Vec::new(), // TODO: Parse graph paths
    })
}

/// Download elle-cli JAR
pub fn download_elle_cli(target_dir: &Path) -> Result<PathBuf, ElleError> {
    std::fs::create_dir_all(target_dir)?;

    let target_path = target_dir.join(ELLE_CLI_JAR_NAME);

    info!("Downloading elle-cli from {}", ELLE_CLI_URL);

    // Use curl or wget (platform-dependent)
    #[cfg(windows)]
    {
        let status = Command::new("powershell")
            .args([
                "-Command",
                &format!(
                    "Invoke-WebRequest -Uri '{}' -OutFile '{}'",
                    ELLE_CLI_URL,
                    target_path.display()
                ),
            ])
            .status()?;

        if !status.success() {
            return Err(ElleError::DownloadFailed("PowerShell download failed".to_string()));
        }
    }

    #[cfg(not(windows))]
    {
        // Try curl first, then wget
        let status = Command::new("curl")
            .args(["-L", "-o", target_path.to_str().unwrap(), ELLE_CLI_URL])
            .status();

        if status.is_err() || !status.unwrap().success() {
            let status = Command::new("wget")
                .args(["-O", target_path.to_str().unwrap(), ELLE_CLI_URL])
                .status()?;

            if !status.success() {
                return Err(ElleError::DownloadFailed("Both curl and wget failed".to_string()));
            }
        }
    }

    if target_path.exists() {
        info!("Downloaded elle-cli to {}", target_path.display());
        Ok(target_path)
    } else {
        Err(ElleError::DownloadFailed("File not created".to_string()))
    }
}

/// Get home directory
fn dirs_home() -> Option<PathBuf> {
    #[cfg(windows)]
    {
        std::env::var("USERPROFILE").ok().map(PathBuf::from)
    }
    #[cfg(not(windows))]
    {
        std::env::var("HOME").ok().map(PathBuf::from)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_elle_output_valid() {
        let output = "Analyzing history...\nNo anomalies found.\nDone.";
        let result = parse_elle_output(output, true).unwrap();

        assert!(result.valid);
        assert!(result.anomaly.is_none());
    }

    #[test]
    fn test_parse_elle_output_g2() {
        let output = "Analyzing history...\nFound G2 anomaly in cycle...\nInvalid.";
        let result = parse_elle_output(output, false).unwrap();

        assert!(!result.valid);
        assert_eq!(result.anomaly, Some("G2 (anti-dependency cycle)".to_string()));
    }

    #[test]
    fn test_parse_elle_output_lost_update() {
        let output = "Lost update detected between txn 1 and txn 2";
        let result = parse_elle_output(output, false).unwrap();

        assert!(!result.valid);
        assert_eq!(result.anomaly, Some("Lost update".to_string()));
    }

    #[test]
    fn test_resolve_jar_not_found() {
        let result = resolve_elle_jar(Some(PathBuf::from("/nonexistent/path/elle.jar")));
        assert!(matches!(result, Err(ElleError::JarNotFound(_))));
    }
}
