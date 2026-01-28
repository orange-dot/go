//! EKKL Error Types
//!
//! Unified error types for lexing, parsing, type checking, and evaluation.

use std::fmt;
use thiserror::Error;

/// Source location for error reporting
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Span {
    pub start: usize,
    pub end: usize,
    pub line: usize,
    pub column: usize,
}

impl Span {
    pub fn new(start: usize, end: usize, line: usize, column: usize) -> Self {
        Self { start, end, line, column }
    }
}

impl fmt::Display for Span {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}:{}", self.line, self.column)
    }
}

/// Lexer error
#[derive(Debug, Error)]
pub enum LexError {
    #[error("unexpected character '{0}' at {1}")]
    UnexpectedChar(char, Span),

    #[error("unterminated string literal at {0}")]
    UnterminatedString(Span),

    #[error("invalid number literal at {0}")]
    InvalidNumber(Span),

    #[error("invalid fixed-point literal at {0}")]
    InvalidFixed(Span),
}

/// Parser error
#[derive(Debug, Error)]
pub enum ParseError {
    #[error("unexpected token: expected {expected}, found {found} at {span}")]
    UnexpectedToken {
        expected: String,
        found: String,
        span: Span,
    },

    #[error("unexpected end of input")]
    UnexpectedEof,

    #[error("invalid expression at {0}")]
    InvalidExpr(Span),

    #[error("invalid type at {0}")]
    InvalidType(Span),

    #[error("nom parse error: {0}")]
    NomError(String),
}

/// Type checking error
#[derive(Debug, Error)]
pub enum TypeError {
    #[error("undefined variable '{name}' at {span}")]
    UndefinedVariable { name: String, span: Span },

    #[error("undefined function '{name}' at {span}")]
    UndefinedFunction { name: String, span: Span },

    #[error("undefined type '{name}' at {span}")]
    UndefinedType { name: String, span: Span },

    #[error("type mismatch: expected {expected}, found {found} at {span}")]
    TypeMismatch {
        expected: String,
        found: String,
        span: Span,
    },

    #[error("cannot apply operator {op} to types {left} and {right} at {span}")]
    InvalidOperator {
        op: String,
        left: String,
        right: String,
        span: Span,
    },

    #[error("wrong number of arguments: expected {expected}, found {found} at {span}")]
    ArityMismatch {
        expected: usize,
        found: usize,
        span: Span,
    },

    #[error("field '{field}' not found in struct '{struct_name}' at {span}")]
    FieldNotFound {
        field: String,
        struct_name: String,
        span: Span,
    },

    #[error("duplicate definition of '{name}' at {span}")]
    DuplicateDefinition { name: String, span: Span },
}

/// Evaluation error
#[derive(Debug, Error)]
pub enum EvalError {
    #[error("undefined variable '{0}'")]
    UndefinedVariable(String),

    #[error("undefined function '{0}'")]
    UndefinedFunction(String),

    #[error("type error: {0}")]
    TypeError(String),

    #[error("division by zero")]
    DivisionByZero,

    #[error("overflow")]
    Overflow,

    #[error("index out of bounds: {index} >= {len}")]
    IndexOutOfBounds { index: usize, len: usize },

    #[error("assertion failed: {0}")]
    AssertionFailed(String),

    #[error("stack overflow")]
    StackOverflow,

    #[error("return value")]
    Return(Box<crate::value::Value>),

    #[error("invalid cast from {from} to {to}")]
    InvalidCast { from: String, to: String },
}

/// Combined EKKL error
#[derive(Debug, Error)]
pub enum EkklError {
    #[error("lexer error: {0}")]
    Lex(#[from] LexError),

    #[error("parse error: {0}")]
    Parse(#[from] ParseError),

    #[error("type error: {0}")]
    Type(#[from] TypeError),

    #[error("evaluation error: {0}")]
    Eval(#[from] EvalError),

    #[error("io error: {0}")]
    Io(#[from] std::io::Error),
}

pub type Result<T> = std::result::Result<T, EkklError>;
