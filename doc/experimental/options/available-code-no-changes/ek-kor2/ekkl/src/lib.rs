//! EKKL: EK-KOR Language
//!
//! A minimal, deterministic language for EK-KOR2 test vector oracles.
//! Provides native Q16.16 fixed-point support and implements all EKK module logic.
//!
//! # Features
//!
//! - Native Q16.16 fixed-point arithmetic (using `fixed` crate)
//! - Deterministic evaluation (no floating-point in semantics)
//! - Static type checking
//! - Simple, Rust-like syntax
//!
//! # Example
//!
//! ```ignore
//! use ekkl::{Evaluator, parse};
//!
//! let source = r#"
//!     fn gradient(my_load: q16_16, neighbor_load: q16_16) -> q16_16 {
//!         return neighbor_load - my_load
//!     }
//! "#;
//!
//! let program = parse(source)?;
//! let mut eval = Evaluator::new();
//! eval.load_program(&program)?;
//!
//! let result = eval.call("gradient", vec![
//!     Value::Fixed(Fixed::from_num(0.3)),
//!     Value::Fixed(Fixed::from_num(0.7)),
//! ])?;
//! ```

pub mod ast;
pub mod builtins;
pub mod error;
pub mod eval;
pub mod lexer;
pub mod parser;
pub mod types;
pub mod value;

// Re-exports for convenience
pub use ast::{Program, Type};
pub use error::{EkklError, EvalError, Result};
pub use eval::Evaluator;
pub use fixed::types::I16F16 as Fixed;
pub use parser::parse;
pub use value::Value;

/// Version of the EKKL interpreter
pub const VERSION: &str = env!("CARGO_PKG_VERSION");

/// Parse and evaluate EKKL source code
pub fn run(source: &str) -> Result<Value> {
    let program = parse(source)?;
    let mut eval = Evaluator::new();
    eval.load_program(&program)?;

    // If there's a main function, call it
    if eval.has_function("main") {
        Ok(eval.call("main", vec![])?)
    } else {
        Ok(Value::Unit)
    }
}

/// Parse EKKL source and return the AST
pub fn compile(source: &str) -> Result<Program> {
    parse(source)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_basic_eval() {
        let source = r#"
            fn add(a: i32, b: i32) -> i32 {
                return a + b
            }

            fn main() -> i32 {
                return add(1, 2)
            }
        "#;

        let result = run(source).unwrap();
        assert_eq!(result, Value::I32(3));
    }

    #[test]
    fn test_fixed_point() {
        let source = r#"
            fn gradient(my_load: q16_16, neighbor_load: q16_16) -> q16_16 {
                return neighbor_load - my_load
            }

            fn main() -> q16_16 {
                return gradient(0.3q, 0.7q)
            }
        "#;

        let result = run(source).unwrap();
        if let Value::Fixed(f) = result {
            // Should be approximately 0.4
            let expected = Fixed::from_num(0.4);
            let diff = (f - expected).abs();
            assert!(diff < Fixed::from_num(0.001));
        } else {
            panic!("Expected Fixed value");
        }
    }
}
