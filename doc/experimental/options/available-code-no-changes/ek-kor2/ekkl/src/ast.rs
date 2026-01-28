//! EKKL Abstract Syntax Tree
//!
//! Defines all AST node types for the EKKL language.

use crate::error::Span;

/// A complete EKKL program
#[derive(Debug, Clone)]
pub struct Program {
    pub items: Vec<Item>,
}

/// Top-level item
#[derive(Debug, Clone)]
pub enum Item {
    /// Function definition
    FnDef(FnDef),
    /// Struct definition
    StructDef(StructDef),
    /// Enum definition
    EnumDef(EnumDef),
    /// Constant definition
    ConstDef(ConstDef),
}

/// Function definition
#[derive(Debug, Clone)]
pub struct FnDef {
    pub name: String,
    pub params: Vec<Param>,
    pub return_type: Option<Type>,
    pub body: Block,
    pub span: Span,
}

/// Function parameter
#[derive(Debug, Clone)]
pub struct Param {
    pub name: String,
    pub ty: Type,
    pub span: Span,
}

/// Struct definition
#[derive(Debug, Clone)]
pub struct StructDef {
    pub name: String,
    pub fields: Vec<StructField>,
    pub span: Span,
}

/// Struct field
#[derive(Debug, Clone)]
pub struct StructField {
    pub name: String,
    pub ty: Type,
    pub span: Span,
}

/// Enum definition
#[derive(Debug, Clone)]
pub struct EnumDef {
    pub name: String,
    pub variants: Vec<String>,
    pub span: Span,
}

/// Constant definition
#[derive(Debug, Clone)]
pub struct ConstDef {
    pub name: String,
    pub ty: Type,
    pub value: Expr,
    pub span: Span,
}

/// Block of statements
#[derive(Debug, Clone)]
pub struct Block {
    pub stmts: Vec<Stmt>,
    pub span: Span,
}

/// Statement
#[derive(Debug, Clone)]
pub enum Stmt {
    /// Variable binding: let x: T = expr
    Let {
        name: String,
        mutable: bool,
        ty: Option<Type>,
        init: Expr,
        span: Span,
    },
    /// Assignment: x = expr
    Assign {
        target: AssignTarget,
        value: Expr,
        span: Span,
    },
    /// Return statement
    Return {
        value: Option<Expr>,
        span: Span,
    },
    /// If statement
    If {
        cond: Expr,
        then_block: Block,
        else_block: Option<Block>,
        span: Span,
    },
    /// Match statement
    Match {
        expr: Expr,
        arms: Vec<MatchArm>,
        span: Span,
    },
    /// For loop
    For {
        var: String,
        start: Expr,
        end: Expr,
        body: Block,
        span: Span,
    },
    /// While loop
    While {
        cond: Expr,
        body: Block,
        span: Span,
    },
    /// Expression statement
    Expr {
        expr: Expr,
        span: Span,
    },
}

/// Assignment target
#[derive(Debug, Clone)]
pub enum AssignTarget {
    /// Simple variable
    Ident(String),
    /// Field access: a.b
    Field { base: Box<AssignTarget>, field: String },
    /// Array index: a[i]
    Index { base: Box<AssignTarget>, index: Expr },
}

/// Match arm
#[derive(Debug, Clone)]
pub struct MatchArm {
    pub pattern: Pattern,
    pub body: Expr,
    pub span: Span,
}

/// Pattern for matching
#[derive(Debug, Clone)]
pub enum Pattern {
    /// Wildcard: _
    Wildcard,
    /// Identifier binding
    Ident(String),
    /// Enum variant
    Variant { enum_name: Option<String>, variant: String },
    /// Literal value
    Literal(Literal),
}

/// Expression
#[derive(Debug, Clone)]
pub enum Expr {
    /// Literal value
    Literal {
        value: Literal,
        span: Span,
    },
    /// Variable reference
    Ident {
        name: String,
        span: Span,
    },
    /// Binary operation
    Binary {
        op: BinOp,
        left: Box<Expr>,
        right: Box<Expr>,
        span: Span,
    },
    /// Unary operation
    Unary {
        op: UnOp,
        expr: Box<Expr>,
        span: Span,
    },
    /// Function call
    Call {
        func: String,
        args: Vec<Expr>,
        span: Span,
    },
    /// Method call: expr.method(args)
    MethodCall {
        receiver: Box<Expr>,
        method: String,
        args: Vec<Expr>,
        span: Span,
    },
    /// Field access: expr.field
    FieldAccess {
        expr: Box<Expr>,
        field: String,
        span: Span,
    },
    /// Array index: expr[index]
    Index {
        expr: Box<Expr>,
        index: Box<Expr>,
        span: Span,
    },
    /// Struct initialization: Name { field: value, ... }
    StructInit {
        name: String,
        fields: Vec<(String, Expr)>,
        span: Span,
    },
    /// Array initialization: [a, b, c]
    ArrayInit {
        elements: Vec<Expr>,
        span: Span,
    },
    /// Type cast: expr as Type
    Cast {
        expr: Box<Expr>,
        ty: Type,
        span: Span,
    },
    /// Conditional expression: if cond { a } else { b }
    IfExpr {
        cond: Box<Expr>,
        then_expr: Box<Expr>,
        else_expr: Box<Expr>,
        span: Span,
    },
    /// Block expression
    Block {
        block: Block,
        span: Span,
    },
}

impl Expr {
    pub fn span(&self) -> Span {
        match self {
            Expr::Literal { span, .. }
            | Expr::Ident { span, .. }
            | Expr::Binary { span, .. }
            | Expr::Unary { span, .. }
            | Expr::Call { span, .. }
            | Expr::MethodCall { span, .. }
            | Expr::FieldAccess { span, .. }
            | Expr::Index { span, .. }
            | Expr::StructInit { span, .. }
            | Expr::ArrayInit { span, .. }
            | Expr::Cast { span, .. }
            | Expr::IfExpr { span, .. }
            | Expr::Block { span, .. } => *span,
        }
    }
}

/// Literal value
#[derive(Debug, Clone)]
pub enum Literal {
    /// Integer literal
    Int(i64),
    /// Fixed-point literal (Q16.16 as raw i32)
    Fixed(i32),
    /// Boolean literal
    Bool(bool),
    /// String literal
    String(String),
    /// Unit literal ()
    Unit,
}

/// Binary operator
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BinOp {
    // Arithmetic
    Add,
    Sub,
    Mul,
    Div,
    Mod,

    // Comparison
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,

    // Logical
    And,
    Or,

    // Bitwise
    BitAnd,
    BitOr,
    BitXor,
    Shl,
    Shr,
}

impl BinOp {
    pub fn as_str(&self) -> &'static str {
        match self {
            BinOp::Add => "+",
            BinOp::Sub => "-",
            BinOp::Mul => "*",
            BinOp::Div => "/",
            BinOp::Mod => "%",
            BinOp::Eq => "==",
            BinOp::Ne => "!=",
            BinOp::Lt => "<",
            BinOp::Le => "<=",
            BinOp::Gt => ">",
            BinOp::Ge => ">=",
            BinOp::And => "&&",
            BinOp::Or => "||",
            BinOp::BitAnd => "&",
            BinOp::BitOr => "|",
            BinOp::BitXor => "^",
            BinOp::Shl => "<<",
            BinOp::Shr => ">>",
        }
    }
}

/// Unary operator
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UnOp {
    /// Logical not
    Not,
    /// Arithmetic negation
    Neg,
    /// Bitwise not
    BitNot,
}

impl UnOp {
    pub fn as_str(&self) -> &'static str {
        match self {
            UnOp::Not => "!",
            UnOp::Neg => "-",
            UnOp::BitNot => "~",
        }
    }
}

/// Type
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Type {
    // Primitives
    I8,
    I16,
    I32,
    I64,
    U8,
    U16,
    U32,
    U64,
    Bool,
    /// Q16.16 fixed-point
    Q16_16,
    /// Unit type
    Unit,

    /// Array type: [T; N]
    Array {
        elem: Box<Type>,
        size: usize,
    },

    /// Named type (struct or enum)
    Named(String),

    /// Result type
    Result {
        ok: Box<Type>,
        err: Box<Type>,
    },

    /// Option type
    Option(Box<Type>),

    /// Inferred type (for type checking)
    Infer,
}

impl Type {
    pub fn is_numeric(&self) -> bool {
        matches!(
            self,
            Type::I8 | Type::I16 | Type::I32 | Type::I64 |
            Type::U8 | Type::U16 | Type::U32 | Type::U64 |
            Type::Q16_16
        )
    }

    pub fn is_integer(&self) -> bool {
        matches!(
            self,
            Type::I8 | Type::I16 | Type::I32 | Type::I64 |
            Type::U8 | Type::U16 | Type::U32 | Type::U64
        )
    }

    pub fn is_signed(&self) -> bool {
        matches!(self, Type::I8 | Type::I16 | Type::I32 | Type::I64 | Type::Q16_16)
    }
}

impl std::fmt::Display for Type {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Type::I8 => write!(f, "i8"),
            Type::I16 => write!(f, "i16"),
            Type::I32 => write!(f, "i32"),
            Type::I64 => write!(f, "i64"),
            Type::U8 => write!(f, "u8"),
            Type::U16 => write!(f, "u16"),
            Type::U32 => write!(f, "u32"),
            Type::U64 => write!(f, "u64"),
            Type::Bool => write!(f, "bool"),
            Type::Q16_16 => write!(f, "q16_16"),
            Type::Unit => write!(f, "()"),
            Type::Array { elem, size } => write!(f, "[{}; {}]", elem, size),
            Type::Named(name) => write!(f, "{}", name),
            Type::Result { ok, err } => write!(f, "Result<{}, {}>", ok, err),
            Type::Option(inner) => write!(f, "Option<{}>", inner),
            Type::Infer => write!(f, "_"),
        }
    }
}
