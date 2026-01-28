//! EK-KOR2 Property-Based Tests
//!
//! Standalone test suite using proptest for mathematical invariant verification.
//! This project is completely isolated from the main ekk crate.
//!
//! # Usage
//!
//! ```bash
//! cd spec/property-tests
//! cargo test                    # Run all property tests
//! cargo test types              # Run only types module tests
//! cargo test field              # Run only field module tests
//! cargo test -- --nocapture     # Show output
//! PROPTEST_CASES=1000 cargo test  # More test cases
//! ```
//!
//! # Test Modules
//!
//! - `types`: Position, Field, Fixed-point invariants
//! - `field`: Gradient antisymmetry, decay monotonicity
//! - `consensus`: Vote counting, quorum math
//! - `heartbeat`: State transitions
//! - `topology`: k-neighbor properties

// Re-export for convenience in tests
pub use ekk::*;
