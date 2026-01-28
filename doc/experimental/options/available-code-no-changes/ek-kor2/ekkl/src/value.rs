//! EKKL Runtime Values
//!
//! Defines the runtime representation of EKKL values.

use std::collections::HashMap;
use std::fmt;

use fixed::types::I16F16;

use crate::error::EvalError;

/// Q16.16 fixed-point type alias
pub type Fixed = I16F16;

/// Runtime value
#[derive(Debug, Clone)]
pub enum Value {
    // Integer types
    I8(i8),
    I16(i16),
    I32(i32),
    I64(i64),
    U8(u8),
    U16(u16),
    U32(u32),
    U64(u64),

    // Boolean
    Bool(bool),

    // Fixed-point Q16.16
    Fixed(Fixed),

    // String
    String(String),

    // Array
    Array(Vec<Value>),

    // Struct
    Struct {
        name: String,
        fields: HashMap<String, Value>,
    },

    // Enum variant
    Enum {
        enum_name: String,
        variant: String,
    },

    // Unit (void)
    Unit,
}

impl Value {
    // ========================================================================
    // Constructors
    // ========================================================================

    /// Create a fixed-point value from a float
    pub fn fixed_from_f32(f: f32) -> Self {
        Value::Fixed(Fixed::from_num(f))
    }

    /// Create a fixed-point value from raw bits
    pub fn fixed_from_bits(bits: i32) -> Self {
        Value::Fixed(Fixed::from_bits(bits))
    }

    /// Create a zero fixed-point value
    pub fn fixed_zero() -> Self {
        Value::Fixed(Fixed::ZERO)
    }

    /// Create a one fixed-point value
    pub fn fixed_one() -> Self {
        Value::Fixed(Fixed::ONE)
    }

    // ========================================================================
    // Type checking
    // ========================================================================

    pub fn type_name(&self) -> &'static str {
        match self {
            Value::I8(_) => "i8",
            Value::I16(_) => "i16",
            Value::I32(_) => "i32",
            Value::I64(_) => "i64",
            Value::U8(_) => "u8",
            Value::U16(_) => "u16",
            Value::U32(_) => "u32",
            Value::U64(_) => "u64",
            Value::Bool(_) => "bool",
            Value::Fixed(_) => "q16_16",
            Value::String(_) => "String",
            Value::Array(_) => "array",
            Value::Struct { .. } => "struct",
            Value::Enum { .. } => "enum",
            Value::Unit => "()",
        }
    }

    pub fn is_numeric(&self) -> bool {
        matches!(
            self,
            Value::I8(_)
                | Value::I16(_)
                | Value::I32(_)
                | Value::I64(_)
                | Value::U8(_)
                | Value::U16(_)
                | Value::U32(_)
                | Value::U64(_)
                | Value::Fixed(_)
        )
    }

    pub fn is_integer(&self) -> bool {
        matches!(
            self,
            Value::I8(_)
                | Value::I16(_)
                | Value::I32(_)
                | Value::I64(_)
                | Value::U8(_)
                | Value::U16(_)
                | Value::U32(_)
                | Value::U64(_)
        )
    }

    // ========================================================================
    // Extraction methods
    // ========================================================================

    pub fn as_i8(&self) -> Result<i8, EvalError> {
        match self {
            Value::I8(v) => Ok(*v),
            Value::I16(v) => Ok(*v as i8),
            Value::I32(v) => Ok(*v as i8),
            Value::I64(v) => Ok(*v as i8),
            Value::U8(v) => Ok(*v as i8),
            Value::U16(v) => Ok(*v as i8),
            Value::U32(v) => Ok(*v as i8),
            Value::U64(v) => Ok(*v as i8),
            _ => Err(EvalError::TypeError(format!(
                "expected i8, got {}",
                self.type_name()
            ))),
        }
    }

    pub fn as_i16(&self) -> Result<i16, EvalError> {
        match self {
            Value::I8(v) => Ok(*v as i16),
            Value::I16(v) => Ok(*v),
            Value::I32(v) => Ok(*v as i16),
            Value::I64(v) => Ok(*v as i16),
            Value::U8(v) => Ok(*v as i16),
            Value::U16(v) => Ok(*v as i16),
            Value::U32(v) => Ok(*v as i16),
            Value::U64(v) => Ok(*v as i16),
            _ => Err(EvalError::TypeError(format!(
                "expected i16, got {}",
                self.type_name()
            ))),
        }
    }

    pub fn as_i32(&self) -> Result<i32, EvalError> {
        match self {
            Value::I8(v) => Ok(*v as i32),
            Value::I16(v) => Ok(*v as i32),
            Value::I32(v) => Ok(*v),
            Value::I64(v) => Ok(*v as i32),
            Value::U8(v) => Ok(*v as i32),
            Value::U16(v) => Ok(*v as i32),
            Value::U32(v) => Ok(*v as i32),
            Value::U64(v) => Ok(*v as i32),
            Value::Fixed(f) => Ok(f.to_bits()),
            _ => Err(EvalError::TypeError(format!(
                "expected i32, got {}",
                self.type_name()
            ))),
        }
    }

    pub fn as_i64(&self) -> Result<i64, EvalError> {
        match self {
            Value::I8(v) => Ok(*v as i64),
            Value::I16(v) => Ok(*v as i64),
            Value::I32(v) => Ok(*v as i64),
            Value::I64(v) => Ok(*v),
            Value::U8(v) => Ok(*v as i64),
            Value::U16(v) => Ok(*v as i64),
            Value::U32(v) => Ok(*v as i64),
            Value::U64(v) => Ok(*v as i64),
            _ => Err(EvalError::TypeError(format!(
                "expected i64, got {}",
                self.type_name()
            ))),
        }
    }

    pub fn as_u8(&self) -> Result<u8, EvalError> {
        match self {
            Value::I8(v) => Ok(*v as u8),
            Value::I16(v) => Ok(*v as u8),
            Value::I32(v) => Ok(*v as u8),
            Value::I64(v) => Ok(*v as u8),
            Value::U8(v) => Ok(*v),
            Value::U16(v) => Ok(*v as u8),
            Value::U32(v) => Ok(*v as u8),
            Value::U64(v) => Ok(*v as u8),
            _ => Err(EvalError::TypeError(format!(
                "expected u8, got {}",
                self.type_name()
            ))),
        }
    }

    pub fn as_u16(&self) -> Result<u16, EvalError> {
        match self {
            Value::I8(v) => Ok(*v as u16),
            Value::I16(v) => Ok(*v as u16),
            Value::I32(v) => Ok(*v as u16),
            Value::I64(v) => Ok(*v as u16),
            Value::U8(v) => Ok(*v as u16),
            Value::U16(v) => Ok(*v),
            Value::U32(v) => Ok(*v as u16),
            Value::U64(v) => Ok(*v as u16),
            _ => Err(EvalError::TypeError(format!(
                "expected u16, got {}",
                self.type_name()
            ))),
        }
    }

    pub fn as_u32(&self) -> Result<u32, EvalError> {
        match self {
            Value::I8(v) => Ok(*v as u32),
            Value::I16(v) => Ok(*v as u32),
            Value::I32(v) => Ok(*v as u32),
            Value::I64(v) => Ok(*v as u32),
            Value::U8(v) => Ok(*v as u32),
            Value::U16(v) => Ok(*v as u32),
            Value::U32(v) => Ok(*v),
            Value::U64(v) => Ok(*v as u32),
            _ => Err(EvalError::TypeError(format!(
                "expected u32, got {}",
                self.type_name()
            ))),
        }
    }

    pub fn as_u64(&self) -> Result<u64, EvalError> {
        match self {
            Value::I8(v) => Ok(*v as u64),
            Value::I16(v) => Ok(*v as u64),
            Value::I32(v) => Ok(*v as u64),
            Value::I64(v) => Ok(*v as u64),
            Value::U8(v) => Ok(*v as u64),
            Value::U16(v) => Ok(*v as u64),
            Value::U32(v) => Ok(*v as u64),
            Value::U64(v) => Ok(*v),
            _ => Err(EvalError::TypeError(format!(
                "expected u64, got {}",
                self.type_name()
            ))),
        }
    }

    pub fn as_bool(&self) -> Result<bool, EvalError> {
        match self {
            Value::Bool(b) => Ok(*b),
            _ => Err(EvalError::TypeError(format!(
                "expected bool, got {}",
                self.type_name()
            ))),
        }
    }

    pub fn as_fixed(&self) -> Result<Fixed, EvalError> {
        match self {
            Value::Fixed(f) => Ok(*f),
            Value::I8(v) => Ok(Fixed::from_num(*v)),
            Value::I16(v) => Ok(Fixed::from_num(*v)),
            Value::I32(v) => Ok(Fixed::from_num(*v)),
            Value::I64(v) => Ok(Fixed::from_num(*v as i32)),
            Value::U8(v) => Ok(Fixed::from_num(*v)),
            Value::U16(v) => Ok(Fixed::from_num(*v)),
            Value::U32(v) => Ok(Fixed::from_num(*v as i32)),
            Value::U64(v) => Ok(Fixed::from_num(*v as i32)),
            _ => Err(EvalError::TypeError(format!(
                "expected q16_16, got {}",
                self.type_name()
            ))),
        }
    }

    pub fn as_string(&self) -> Result<&str, EvalError> {
        match self {
            Value::String(s) => Ok(s),
            _ => Err(EvalError::TypeError(format!(
                "expected String, got {}",
                self.type_name()
            ))),
        }
    }

    pub fn as_array(&self) -> Result<&[Value], EvalError> {
        match self {
            Value::Array(arr) => Ok(arr),
            _ => Err(EvalError::TypeError(format!(
                "expected array, got {}",
                self.type_name()
            ))),
        }
    }

    pub fn as_array_mut(&mut self) -> Result<&mut Vec<Value>, EvalError> {
        match self {
            Value::Array(arr) => Ok(arr),
            _ => Err(EvalError::TypeError(format!(
                "expected array, got {}",
                self.type_name()
            ))),
        }
    }

    pub fn get_field(&self, field: &str) -> Result<&Value, EvalError> {
        match self {
            Value::Struct { fields, .. } => {
                fields.get(field).ok_or_else(|| {
                    EvalError::TypeError(format!("field '{}' not found", field))
                })
            }
            _ => Err(EvalError::TypeError(format!(
                "expected struct, got {}",
                self.type_name()
            ))),
        }
    }

    pub fn set_field(&mut self, field: &str, value: Value) -> Result<(), EvalError> {
        match self {
            Value::Struct { fields, .. } => {
                fields.insert(field.to_string(), value);
                Ok(())
            }
            _ => Err(EvalError::TypeError(format!(
                "expected struct, got {}",
                self.type_name()
            ))),
        }
    }

    pub fn as_enum_variant(&self) -> Result<(&str, &str), EvalError> {
        match self {
            Value::Enum { enum_name, variant } => Ok((enum_name, variant)),
            _ => Err(EvalError::TypeError(format!(
                "expected enum, got {}",
                self.type_name()
            ))),
        }
    }

    // ========================================================================
    // Conversion to JSON-compatible values
    // ========================================================================

    pub fn to_json(&self) -> serde_json::Value {
        match self {
            Value::I8(v) => serde_json::Value::Number((*v as i64).into()),
            Value::I16(v) => serde_json::Value::Number((*v as i64).into()),
            Value::I32(v) => serde_json::Value::Number((*v as i64).into()),
            Value::I64(v) => serde_json::Value::Number((*v).into()),
            Value::U8(v) => serde_json::Value::Number((*v as u64).into()),
            Value::U16(v) => serde_json::Value::Number((*v as u64).into()),
            Value::U32(v) => serde_json::Value::Number((*v as u64).into()),
            Value::U64(v) => serde_json::Value::Number((*v).into()),
            Value::Bool(b) => serde_json::Value::Bool(*b),
            Value::Fixed(f) => {
                // Return as raw bits for determinism
                serde_json::Value::Number(f.to_bits().into())
            }
            Value::String(s) => serde_json::Value::String(s.clone()),
            Value::Array(arr) => {
                serde_json::Value::Array(arr.iter().map(|v| v.to_json()).collect())
            }
            Value::Struct { fields, .. } => {
                let map: serde_json::Map<_, _> = fields
                    .iter()
                    .map(|(k, v)| (k.clone(), v.to_json()))
                    .collect();
                serde_json::Value::Object(map)
            }
            Value::Enum { variant, .. } => serde_json::Value::String(variant.clone()),
            Value::Unit => serde_json::Value::Null,
        }
    }
}

impl PartialEq for Value {
    fn eq(&self, other: &Self) -> bool {
        match (self, other) {
            (Value::I8(a), Value::I8(b)) => a == b,
            (Value::I16(a), Value::I16(b)) => a == b,
            (Value::I32(a), Value::I32(b)) => a == b,
            (Value::I64(a), Value::I64(b)) => a == b,
            (Value::U8(a), Value::U8(b)) => a == b,
            (Value::U16(a), Value::U16(b)) => a == b,
            (Value::U32(a), Value::U32(b)) => a == b,
            (Value::U64(a), Value::U64(b)) => a == b,
            (Value::Bool(a), Value::Bool(b)) => a == b,
            (Value::Fixed(a), Value::Fixed(b)) => a == b,
            (Value::String(a), Value::String(b)) => a == b,
            (Value::Unit, Value::Unit) => true,
            (Value::Enum { enum_name: e1, variant: v1 }, Value::Enum { enum_name: e2, variant: v2 }) => {
                e1 == e2 && v1 == v2
            }
            // Cross-type integer comparison
            (a, b) if a.is_integer() && b.is_integer() => {
                a.as_i64().ok() == b.as_i64().ok()
            }
            _ => false,
        }
    }
}

impl Eq for Value {}

impl fmt::Display for Value {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Value::I8(v) => write!(f, "{}", v),
            Value::I16(v) => write!(f, "{}", v),
            Value::I32(v) => write!(f, "{}", v),
            Value::I64(v) => write!(f, "{}", v),
            Value::U8(v) => write!(f, "{}", v),
            Value::U16(v) => write!(f, "{}", v),
            Value::U32(v) => write!(f, "{}", v),
            Value::U64(v) => write!(f, "{}", v),
            Value::Bool(b) => write!(f, "{}", b),
            Value::Fixed(fp) => write!(f, "{}", fp),
            Value::String(s) => write!(f, "\"{}\"", s),
            Value::Array(arr) => {
                write!(f, "[")?;
                for (i, v) in arr.iter().enumerate() {
                    if i > 0 {
                        write!(f, ", ")?;
                    }
                    write!(f, "{}", v)?;
                }
                write!(f, "]")
            }
            Value::Struct { name, fields } => {
                write!(f, "{} {{ ", name)?;
                for (i, (k, v)) in fields.iter().enumerate() {
                    if i > 0 {
                        write!(f, ", ")?;
                    }
                    write!(f, "{}: {}", k, v)?;
                }
                write!(f, " }}")
            }
            Value::Enum { enum_name, variant } => write!(f, "{}::{}", enum_name, variant),
            Value::Unit => write!(f, "()"),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_fixed_from_float() {
        let v = Value::fixed_from_f32(0.5);
        let f = v.as_fixed().unwrap();
        assert_eq!(f, Fixed::from_num(0.5));
    }

    #[test]
    fn test_fixed_from_bits() {
        let v = Value::fixed_from_bits(32768); // 0.5 in Q16.16
        let f = v.as_fixed().unwrap();
        assert_eq!(f, Fixed::from_bits(32768));
    }

    #[test]
    fn test_integer_conversion() {
        let v = Value::I32(42);
        assert_eq!(v.as_i64().unwrap(), 42);
        assert_eq!(v.as_u8().unwrap(), 42);
    }
}
