//! EKKL Built-in Functions
//!
//! Provides built-in functions for Q16.16 operations, array manipulation, and debugging.

use std::collections::HashSet;

use crate::error::EvalError;
use crate::value::{Fixed, Value};

/// Built-in function registry
pub struct Builtins {
    names: HashSet<String>,
}

impl Builtins {
    pub fn new() -> Self {
        let mut names = HashSet::new();

        // Q16.16 operations
        names.insert("q16_from_bits".to_string());
        names.insert("q16_to_bits".to_string());
        names.insert("q16_from_float".to_string());
        names.insert("q16_to_float".to_string());
        names.insert("q16_abs".to_string());
        names.insert("q16_floor".to_string());
        names.insert("q16_ceil".to_string());
        names.insert("q16_round".to_string());
        names.insert("q16_frac".to_string());
        names.insert("q16_min".to_string());
        names.insert("q16_max".to_string());
        names.insert("q16_clamp".to_string());
        names.insert("q16_lerp".to_string());
        names.insert("q16_saturating_add".to_string());
        names.insert("q16_saturating_sub".to_string());
        names.insert("q16_saturating_mul".to_string());

        // Array operations
        names.insert("array_len".to_string());
        names.insert("array_get".to_string());
        names.insert("array_set".to_string());

        // Debug/assert
        names.insert("print".to_string());
        names.insert("assert".to_string());
        names.insert("assert_eq".to_string());

        // Type conversions
        names.insert("to_i32".to_string());
        names.insert("to_i64".to_string());
        names.insert("to_u8".to_string());
        names.insert("to_u16".to_string());
        names.insert("to_u32".to_string());
        names.insert("to_u64".to_string());

        Self { names }
    }

    /// Check if a function name is a built-in
    pub fn has(&self, name: &str) -> bool {
        self.names.contains(name)
    }

    /// Call a built-in function
    pub fn call(&self, name: &str, args: Vec<Value>) -> Result<Value, EvalError> {
        match name {
            // ================================================================
            // Q16.16 operations
            // ================================================================
            "q16_from_bits" => {
                ensure_arity(name, &args, 1)?;
                let bits = args[0].as_i32()?;
                Ok(Value::Fixed(Fixed::from_bits(bits)))
            }
            "q16_to_bits" => {
                ensure_arity(name, &args, 1)?;
                let f = args[0].as_fixed()?;
                Ok(Value::I32(f.to_bits()))
            }
            "q16_from_float" => {
                ensure_arity(name, &args, 1)?;
                // Accept integer and convert to fixed
                let n = args[0].as_i32()?;
                Ok(Value::Fixed(Fixed::from_num(n)))
            }
            "q16_to_float" => {
                ensure_arity(name, &args, 1)?;
                let f = args[0].as_fixed()?;
                // Return as i32 (the integer part)
                Ok(Value::I32(f.to_num::<i32>()))
            }
            "q16_abs" => {
                ensure_arity(name, &args, 1)?;
                let f = args[0].as_fixed()?;
                Ok(Value::Fixed(f.abs()))
            }
            "q16_floor" => {
                ensure_arity(name, &args, 1)?;
                let f = args[0].as_fixed()?;
                Ok(Value::I32(f.floor().to_num()))
            }
            "q16_ceil" => {
                ensure_arity(name, &args, 1)?;
                let f = args[0].as_fixed()?;
                Ok(Value::I32(f.ceil().to_num()))
            }
            "q16_round" => {
                ensure_arity(name, &args, 1)?;
                let f = args[0].as_fixed()?;
                Ok(Value::I32(f.round().to_num()))
            }
            "q16_frac" => {
                ensure_arity(name, &args, 1)?;
                let f = args[0].as_fixed()?;
                Ok(Value::Fixed(f.frac()))
            }
            "q16_min" => {
                ensure_arity(name, &args, 2)?;
                let a = args[0].as_fixed()?;
                let b = args[1].as_fixed()?;
                Ok(Value::Fixed(a.min(b)))
            }
            "q16_max" => {
                ensure_arity(name, &args, 2)?;
                let a = args[0].as_fixed()?;
                let b = args[1].as_fixed()?;
                Ok(Value::Fixed(a.max(b)))
            }
            "q16_clamp" => {
                ensure_arity(name, &args, 3)?;
                let v = args[0].as_fixed()?;
                let min = args[1].as_fixed()?;
                let max = args[2].as_fixed()?;
                Ok(Value::Fixed(v.max(min).min(max)))
            }
            "q16_lerp" => {
                ensure_arity(name, &args, 3)?;
                let a = args[0].as_fixed()?;
                let b = args[1].as_fixed()?;
                let t = args[2].as_fixed()?;
                // lerp(a, b, t) = a + (b - a) * t
                let result = a + (b - a) * t;
                Ok(Value::Fixed(result))
            }
            "q16_saturating_add" => {
                ensure_arity(name, &args, 2)?;
                let a = args[0].as_fixed()?;
                let b = args[1].as_fixed()?;
                Ok(Value::Fixed(a.saturating_add(b)))
            }
            "q16_saturating_sub" => {
                ensure_arity(name, &args, 2)?;
                let a = args[0].as_fixed()?;
                let b = args[1].as_fixed()?;
                Ok(Value::Fixed(a.saturating_sub(b)))
            }
            "q16_saturating_mul" => {
                ensure_arity(name, &args, 2)?;
                let a = args[0].as_fixed()?;
                let b = args[1].as_fixed()?;
                Ok(Value::Fixed(a.saturating_mul(b)))
            }

            // ================================================================
            // Array operations
            // ================================================================
            "array_len" => {
                ensure_arity(name, &args, 1)?;
                let arr = args[0].as_array()?;
                Ok(Value::U64(arr.len() as u64))
            }
            "array_get" => {
                ensure_arity(name, &args, 2)?;
                let arr = args[0].as_array()?;
                let idx = args[1].as_u64()? as usize;
                if idx >= arr.len() {
                    return Err(EvalError::IndexOutOfBounds { index: idx, len: arr.len() });
                }
                Ok(arr[idx].clone())
            }
            "array_set" => {
                // Note: This returns a new array with the element set
                ensure_arity(name, &args, 3)?;
                let arr = args[0].as_array()?;
                let idx = args[1].as_u64()? as usize;
                let val = args[2].clone();
                if idx >= arr.len() {
                    return Err(EvalError::IndexOutOfBounds { index: idx, len: arr.len() });
                }
                let mut new_arr = arr.to_vec();
                new_arr[idx] = val;
                Ok(Value::Array(new_arr))
            }

            // ================================================================
            // Debug/assert
            // ================================================================
            "print" => {
                for arg in &args {
                    println!("{}", arg);
                }
                Ok(Value::Unit)
            }
            "assert" => {
                ensure_arity(name, &args, 1)?;
                let cond = args[0].as_bool()?;
                if !cond {
                    return Err(EvalError::AssertionFailed("assertion failed".to_string()));
                }
                Ok(Value::Unit)
            }
            "assert_eq" => {
                ensure_arity(name, &args, 2)?;
                if args[0] != args[1] {
                    return Err(EvalError::AssertionFailed(format!(
                        "assertion failed: {} != {}",
                        args[0], args[1]
                    )));
                }
                Ok(Value::Unit)
            }

            // ================================================================
            // Type conversions
            // ================================================================
            "to_i32" => {
                ensure_arity(name, &args, 1)?;
                Ok(Value::I32(args[0].as_i32()?))
            }
            "to_i64" => {
                ensure_arity(name, &args, 1)?;
                Ok(Value::I64(args[0].as_i64()?))
            }
            "to_u8" => {
                ensure_arity(name, &args, 1)?;
                Ok(Value::U8(args[0].as_u8()?))
            }
            "to_u16" => {
                ensure_arity(name, &args, 1)?;
                Ok(Value::U16(args[0].as_u16()?))
            }
            "to_u32" => {
                ensure_arity(name, &args, 1)?;
                Ok(Value::U32(args[0].as_u32()?))
            }
            "to_u64" => {
                ensure_arity(name, &args, 1)?;
                Ok(Value::U64(args[0].as_u64()?))
            }

            _ => Err(EvalError::UndefinedFunction(name.to_string())),
        }
    }

    /// Call a method on a value (receiver is first arg)
    pub fn call_method(&self, method: &str, args: Vec<Value>) -> Result<Value, EvalError> {
        if args.is_empty() {
            return Err(EvalError::TypeError("method call requires receiver".to_string()));
        }

        let receiver = &args[0];
        let method_args = &args[1..];

        match receiver {
            Value::Fixed(_) => self.call_fixed_method(method, receiver, method_args),
            Value::Array(_) => self.call_array_method(method, receiver, method_args),
            _ => Err(EvalError::TypeError(format!(
                "no method '{}' on type {}",
                method,
                receiver.type_name()
            ))),
        }
    }

    fn call_fixed_method(
        &self,
        method: &str,
        receiver: &Value,
        args: &[Value],
    ) -> Result<Value, EvalError> {
        let f = receiver.as_fixed()?;

        match method {
            "abs" => Ok(Value::Fixed(f.abs())),
            "floor" => Ok(Value::I32(f.floor().to_num())),
            "ceil" => Ok(Value::I32(f.ceil().to_num())),
            "round" => Ok(Value::I32(f.round().to_num())),
            "frac" => Ok(Value::Fixed(f.frac())),
            "to_bits" => Ok(Value::I32(f.to_bits())),
            "min" => {
                if args.is_empty() {
                    return Err(EvalError::TypeError("min requires argument".to_string()));
                }
                let other = args[0].as_fixed()?;
                Ok(Value::Fixed(f.min(other)))
            }
            "max" => {
                if args.is_empty() {
                    return Err(EvalError::TypeError("max requires argument".to_string()));
                }
                let other = args[0].as_fixed()?;
                Ok(Value::Fixed(f.max(other)))
            }
            "clamp" => {
                if args.len() < 2 {
                    return Err(EvalError::TypeError("clamp requires 2 arguments".to_string()));
                }
                let min = args[0].as_fixed()?;
                let max = args[1].as_fixed()?;
                Ok(Value::Fixed(f.max(min).min(max)))
            }
            _ => Err(EvalError::TypeError(format!(
                "no method '{}' on q16_16",
                method
            ))),
        }
    }

    fn call_array_method(
        &self,
        method: &str,
        receiver: &Value,
        args: &[Value],
    ) -> Result<Value, EvalError> {
        let arr = receiver.as_array()?;

        match method {
            "len" => Ok(Value::U64(arr.len() as u64)),
            "get" => {
                if args.is_empty() {
                    return Err(EvalError::TypeError("get requires index argument".to_string()));
                }
                let idx = args[0].as_u64()? as usize;
                if idx >= arr.len() {
                    return Err(EvalError::IndexOutOfBounds { index: idx, len: arr.len() });
                }
                Ok(arr[idx].clone())
            }
            "first" => {
                if arr.is_empty() {
                    return Err(EvalError::IndexOutOfBounds { index: 0, len: 0 });
                }
                Ok(arr[0].clone())
            }
            "last" => {
                if arr.is_empty() {
                    return Err(EvalError::IndexOutOfBounds { index: 0, len: 0 });
                }
                Ok(arr[arr.len() - 1].clone())
            }
            _ => Err(EvalError::TypeError(format!(
                "no method '{}' on array",
                method
            ))),
        }
    }
}

impl Default for Builtins {
    fn default() -> Self {
        Self::new()
    }
}

fn ensure_arity(name: &str, args: &[Value], expected: usize) -> Result<(), EvalError> {
    if args.len() != expected {
        return Err(EvalError::TypeError(format!(
            "{} expects {} argument(s), got {}",
            name,
            expected,
            args.len()
        )));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_q16_from_bits() {
        let builtins = Builtins::new();
        let result = builtins.call("q16_from_bits", vec![Value::I32(32768)]).unwrap();
        if let Value::Fixed(f) = result {
            assert_eq!(f, Fixed::from_bits(32768)); // 0.5
        } else {
            panic!("Expected Fixed");
        }
    }

    #[test]
    fn test_q16_abs() {
        let builtins = Builtins::new();
        let result = builtins
            .call("q16_abs", vec![Value::Fixed(Fixed::from_num(-0.5))])
            .unwrap();
        if let Value::Fixed(f) = result {
            assert_eq!(f, Fixed::from_num(0.5));
        } else {
            panic!("Expected Fixed");
        }
    }

    #[test]
    fn test_q16_lerp() {
        let builtins = Builtins::new();
        let result = builtins
            .call(
                "q16_lerp",
                vec![
                    Value::Fixed(Fixed::ZERO),
                    Value::Fixed(Fixed::ONE),
                    Value::Fixed(Fixed::from_num(0.5)),
                ],
            )
            .unwrap();
        if let Value::Fixed(f) = result {
            let expected = Fixed::from_num(0.5);
            let diff = (f - expected).abs();
            assert!(diff < Fixed::from_num(0.001));
        } else {
            panic!("Expected Fixed");
        }
    }

    #[test]
    fn test_array_len() {
        let builtins = Builtins::new();
        let arr = Value::Array(vec![Value::I32(1), Value::I32(2), Value::I32(3)]);
        let result = builtins.call("array_len", vec![arr]).unwrap();
        assert_eq!(result.as_u64().unwrap(), 3);
    }

    #[test]
    fn test_assert_eq_pass() {
        let builtins = Builtins::new();
        let result = builtins.call("assert_eq", vec![Value::I32(42), Value::I32(42)]);
        assert!(result.is_ok());
    }

    #[test]
    fn test_assert_eq_fail() {
        let builtins = Builtins::new();
        let result = builtins.call("assert_eq", vec![Value::I32(1), Value::I32(2)]);
        assert!(matches!(result, Err(EvalError::AssertionFailed(_))));
    }
}
