//! EKKL Type System
//!
//! Provides type checking for EKKL programs and predefined EKK types.

use std::collections::HashMap;

use crate::ast::{self, BinOp, Expr, FnDef, Item, Literal, Program, Stmt, Type, UnOp};
use crate::error::{Span, TypeError};

/// Type environment for type checking
#[derive(Debug, Clone)]
pub struct TypeEnv {
    /// Struct definitions
    pub structs: HashMap<String, StructType>,
    /// Enum definitions
    pub enums: HashMap<String, EnumType>,
    /// Function signatures
    pub functions: HashMap<String, FnType>,
    /// Constants
    pub constants: HashMap<String, Type>,
    /// Local variable scopes
    scopes: Vec<HashMap<String, Type>>,
}

/// Struct type information
#[derive(Debug, Clone)]
pub struct StructType {
    pub name: String,
    pub fields: Vec<(String, Type)>,
}

/// Enum type information
#[derive(Debug, Clone)]
pub struct EnumType {
    pub name: String,
    pub variants: Vec<String>,
}

/// Function type information
#[derive(Debug, Clone)]
pub struct FnType {
    pub name: String,
    pub params: Vec<(String, Type)>,
    pub return_type: Type,
}

impl TypeEnv {
    /// Create a new type environment with built-in types
    pub fn new() -> Self {
        let mut env = Self {
            structs: HashMap::new(),
            enums: HashMap::new(),
            functions: HashMap::new(),
            constants: HashMap::new(),
            scopes: vec![HashMap::new()],
        };

        // Register built-in EKK enums
        env.register_builtin_enums();
        env.register_builtin_structs();
        env.register_builtin_constants();

        env
    }

    fn register_builtin_enums(&mut self) {
        // Error enum (matches ekk::types::Error)
        self.enums.insert(
            "Error".to_string(),
            EnumType {
                name: "Error".to_string(),
                variants: vec![
                    "Ok".to_string(),
                    "InvalidArg".to_string(),
                    "NotFound".to_string(),
                    "NoMemory".to_string(),
                    "Timeout".to_string(),
                    "Busy".to_string(),
                    "AlreadyExists".to_string(),
                    "NoQuorum".to_string(),
                    "Inhibited".to_string(),
                    "FieldExpired".to_string(),
                    "NeighborLost".to_string(),
                    "HalFailure".to_string(),
                ],
            },
        );

        // HealthState enum
        self.enums.insert(
            "HealthState".to_string(),
            EnumType {
                name: "HealthState".to_string(),
                variants: vec![
                    "Unknown".to_string(),
                    "Alive".to_string(),
                    "Suspect".to_string(),
                    "Dead".to_string(),
                ],
            },
        );

        // VoteValue enum
        self.enums.insert(
            "VoteValue".to_string(),
            EnumType {
                name: "VoteValue".to_string(),
                variants: vec![
                    "Abstain".to_string(),
                    "Yes".to_string(),
                    "No".to_string(),
                    "Inhibit".to_string(),
                ],
            },
        );

        // VoteResult enum
        self.enums.insert(
            "VoteResult".to_string(),
            EnumType {
                name: "VoteResult".to_string(),
                variants: vec![
                    "Pending".to_string(),
                    "Approved".to_string(),
                    "Rejected".to_string(),
                    "Timeout".to_string(),
                    "Cancelled".to_string(),
                ],
            },
        );

        // ModuleState enum
        self.enums.insert(
            "ModuleState".to_string(),
            EnumType {
                name: "ModuleState".to_string(),
                variants: vec![
                    "Init".to_string(),
                    "Discovering".to_string(),
                    "Active".to_string(),
                    "Degraded".to_string(),
                    "Isolated".to_string(),
                    "Reforming".to_string(),
                    "Shutdown".to_string(),
                ],
            },
        );

        // FieldComponent enum
        self.enums.insert(
            "FieldComponent".to_string(),
            EnumType {
                name: "FieldComponent".to_string(),
                variants: vec![
                    "Load".to_string(),
                    "Thermal".to_string(),
                    "Power".to_string(),
                    "Custom0".to_string(),
                    "Custom1".to_string(),
                ],
            },
        );

        // DecayModel enum
        self.enums.insert(
            "DecayModel".to_string(),
            EnumType {
                name: "DecayModel".to_string(),
                variants: vec![
                    "Exponential".to_string(),
                    "Linear".to_string(),
                    "Step".to_string(),
                ],
            },
        );
    }

    fn register_builtin_structs(&mut self) {
        // Field struct
        self.structs.insert(
            "Field".to_string(),
            StructType {
                name: "Field".to_string(),
                fields: vec![
                    ("components".to_string(), Type::Array {
                        elem: Box::new(Type::Q16_16),
                        size: 5,
                    }),
                    ("timestamp".to_string(), Type::U64),
                    ("source".to_string(), Type::U8),
                    ("sequence".to_string(), Type::U8),
                ],
            },
        );

        // Position struct
        self.structs.insert(
            "Position".to_string(),
            StructType {
                name: "Position".to_string(),
                fields: vec![
                    ("x".to_string(), Type::I16),
                    ("y".to_string(), Type::I16),
                    ("z".to_string(), Type::I16),
                ],
            },
        );

        // Neighbor struct
        self.structs.insert(
            "Neighbor".to_string(),
            StructType {
                name: "Neighbor".to_string(),
                fields: vec![
                    ("id".to_string(), Type::U8),
                    ("health".to_string(), Type::Named("HealthState".to_string())),
                    ("last_seen".to_string(), Type::U64),
                    ("last_field".to_string(), Type::Named("Field".to_string())),
                    ("logical_distance".to_string(), Type::I32),
                    ("missed_heartbeats".to_string(), Type::U8),
                ],
            },
        );
    }

    fn register_builtin_constants(&mut self) {
        // Configuration constants (matching ekk::types)
        self.constants.insert("K_NEIGHBORS".to_string(), Type::U8);
        self.constants.insert("MAX_MODULES".to_string(), Type::U16);
        self.constants.insert("MAX_TASKS_PER_MODULE".to_string(), Type::U8);
        self.constants.insert("FIELD_DECAY_TAU_US".to_string(), Type::U64);
        self.constants.insert("HEARTBEAT_PERIOD_US".to_string(), Type::U64);
        self.constants.insert("HEARTBEAT_TIMEOUT_COUNT".to_string(), Type::U8);
        self.constants.insert("VOTE_TIMEOUT_US".to_string(), Type::U64);
        self.constants.insert("MAX_BALLOTS".to_string(), Type::U8);
        self.constants.insert("FIELD_COUNT".to_string(), Type::U8);
        self.constants.insert("INVALID_MODULE_ID".to_string(), Type::U8);
        self.constants.insert("INVALID_BALLOT_ID".to_string(), Type::U16);
        self.constants.insert("BROADCAST_ID".to_string(), Type::U8);

        // Threshold constants
        self.constants.insert("SIMPLE_MAJORITY".to_string(), Type::Q16_16);
        self.constants.insert("SUPERMAJORITY".to_string(), Type::Q16_16);
        self.constants.insert("UNANIMOUS".to_string(), Type::Q16_16);
    }

    /// Push a new scope
    pub fn push_scope(&mut self) {
        self.scopes.push(HashMap::new());
    }

    /// Pop the current scope
    pub fn pop_scope(&mut self) {
        self.scopes.pop();
    }

    /// Define a variable in the current scope
    pub fn define(&mut self, name: &str, ty: Type) {
        if let Some(scope) = self.scopes.last_mut() {
            scope.insert(name.to_string(), ty);
        }
    }

    /// Look up a variable's type
    pub fn lookup(&self, name: &str) -> Option<&Type> {
        // Check local scopes (innermost first)
        for scope in self.scopes.iter().rev() {
            if let Some(ty) = scope.get(name) {
                return Some(ty);
            }
        }
        // Check constants
        self.constants.get(name)
    }

    /// Look up a struct type
    pub fn get_struct(&self, name: &str) -> Option<&StructType> {
        self.structs.get(name)
    }

    /// Look up an enum type
    pub fn get_enum(&self, name: &str) -> Option<&EnumType> {
        self.enums.get(name)
    }

    /// Look up a function signature
    pub fn get_function(&self, name: &str) -> Option<&FnType> {
        self.functions.get(name)
    }

    /// Check if a type is an enum variant
    pub fn is_enum_variant<'a>(&'a self, name: &'a str) -> Option<(&'a str, &'a str)> {
        // Check for Enum::Variant format
        if let Some(idx) = name.find("::") {
            let enum_name = &name[..idx];
            let variant = &name[idx + 2..];
            if let Some(e) = self.enums.get(enum_name) {
                if e.variants.contains(&variant.to_string()) {
                    return Some((enum_name, variant));
                }
            }
        }
        // Check for just Variant (must search all enums)
        for (enum_name, e) in &self.enums {
            if e.variants.contains(&name.to_string()) {
                return Some((enum_name, name));
            }
        }
        None
    }
}

impl Default for TypeEnv {
    fn default() -> Self {
        Self::new()
    }
}

/// Type checker
pub struct TypeChecker {
    env: TypeEnv,
    errors: Vec<TypeError>,
}

impl TypeChecker {
    pub fn new() -> Self {
        Self {
            env: TypeEnv::new(),
            errors: Vec::new(),
        }
    }

    /// Check a program for type errors
    pub fn check(&mut self, program: &Program) -> Result<TypeEnv, Vec<TypeError>> {
        // First pass: collect all type definitions
        for item in &program.items {
            match item {
                Item::StructDef(s) => {
                    let fields: Vec<_> = s
                        .fields
                        .iter()
                        .map(|f| (f.name.clone(), f.ty.clone()))
                        .collect();
                    self.env.structs.insert(
                        s.name.clone(),
                        StructType {
                            name: s.name.clone(),
                            fields,
                        },
                    );
                }
                Item::EnumDef(e) => {
                    self.env.enums.insert(
                        e.name.clone(),
                        EnumType {
                            name: e.name.clone(),
                            variants: e.variants.clone(),
                        },
                    );
                }
                Item::ConstDef(c) => {
                    self.env.constants.insert(c.name.clone(), c.ty.clone());
                }
                Item::FnDef(f) => {
                    let params: Vec<_> = f
                        .params
                        .iter()
                        .map(|p| (p.name.clone(), p.ty.clone()))
                        .collect();
                    self.env.functions.insert(
                        f.name.clone(),
                        FnType {
                            name: f.name.clone(),
                            params,
                            return_type: f.return_type.clone().unwrap_or(Type::Unit),
                        },
                    );
                }
            }
        }

        // Second pass: type check function bodies
        for item in &program.items {
            if let Item::FnDef(f) = item {
                self.check_fn(f);
            }
        }

        if self.errors.is_empty() {
            Ok(self.env.clone())
        } else {
            Err(std::mem::take(&mut self.errors))
        }
    }

    fn check_fn(&mut self, f: &FnDef) {
        self.env.push_scope();

        // Add parameters to scope
        for param in &f.params {
            self.env.define(&param.name, param.ty.clone());
        }

        // Check body
        for stmt in &f.body.stmts {
            self.check_stmt(stmt, &f.return_type.clone().unwrap_or(Type::Unit));
        }

        self.env.pop_scope();
    }

    fn check_stmt(&mut self, stmt: &Stmt, expected_return: &Type) {
        match stmt {
            Stmt::Let { name, ty, init, span, .. } => {
                let init_ty = self.infer_expr(init);
                let var_ty = ty.clone().unwrap_or(init_ty.clone());

                if !self.types_compatible(&var_ty, &init_ty) {
                    self.errors.push(TypeError::TypeMismatch {
                        expected: format!("{}", var_ty),
                        found: format!("{}", init_ty),
                        span: *span,
                    });
                }

                self.env.define(name, var_ty);
            }
            Stmt::Assign { target, value, span } => {
                let target_ty = self.infer_assign_target(target);
                let value_ty = self.infer_expr(value);

                if !self.types_compatible(&target_ty, &value_ty) {
                    self.errors.push(TypeError::TypeMismatch {
                        expected: format!("{}", target_ty),
                        found: format!("{}", value_ty),
                        span: *span,
                    });
                }
            }
            Stmt::Return { value, span } => {
                let return_ty = value
                    .as_ref()
                    .map(|e| self.infer_expr(e))
                    .unwrap_or(Type::Unit);

                if !self.types_compatible(expected_return, &return_ty) {
                    self.errors.push(TypeError::TypeMismatch {
                        expected: format!("{}", expected_return),
                        found: format!("{}", return_ty),
                        span: *span,
                    });
                }
            }
            Stmt::If { cond, then_block, else_block, span } => {
                let cond_ty = self.infer_expr(cond);
                if cond_ty != Type::Bool {
                    self.errors.push(TypeError::TypeMismatch {
                        expected: "bool".to_string(),
                        found: format!("{}", cond_ty),
                        span: *span,
                    });
                }

                self.env.push_scope();
                for stmt in &then_block.stmts {
                    self.check_stmt(stmt, expected_return);
                }
                self.env.pop_scope();

                if let Some(else_block) = else_block {
                    self.env.push_scope();
                    for stmt in &else_block.stmts {
                        self.check_stmt(stmt, expected_return);
                    }
                    self.env.pop_scope();
                }
            }
            Stmt::Match { expr, arms, .. } => {
                let _expr_ty = self.infer_expr(expr);
                for arm in arms {
                    let _body_ty = self.infer_expr(&arm.body);
                }
            }
            Stmt::For { var, start, end, body, .. } => {
                let start_ty = self.infer_expr(start);
                let end_ty = self.infer_expr(end);

                // Both should be integers
                if !start_ty.is_integer() || !end_ty.is_integer() {
                    // Allow but don't fail silently
                }

                self.env.push_scope();
                self.env.define(var, start_ty);
                for stmt in &body.stmts {
                    self.check_stmt(stmt, expected_return);
                }
                self.env.pop_scope();
            }
            Stmt::While { cond, body, span } => {
                let cond_ty = self.infer_expr(cond);
                if cond_ty != Type::Bool {
                    self.errors.push(TypeError::TypeMismatch {
                        expected: "bool".to_string(),
                        found: format!("{}", cond_ty),
                        span: *span,
                    });
                }

                self.env.push_scope();
                for stmt in &body.stmts {
                    self.check_stmt(stmt, expected_return);
                }
                self.env.pop_scope();
            }
            Stmt::Expr { expr, .. } => {
                self.infer_expr(expr);
            }
        }
    }

    fn infer_expr(&mut self, expr: &Expr) -> Type {
        match expr {
            Expr::Literal { value, .. } => match value {
                Literal::Int(_) => Type::I64, // Default to i64 for untyped integers
                Literal::Fixed(_) => Type::Q16_16,
                Literal::Bool(_) => Type::Bool,
                Literal::String(_) => Type::Named("String".to_string()),
                Literal::Unit => Type::Unit,
            },
            Expr::Ident { name, span } => {
                // Check for enum variant
                if let Some((enum_name, _)) = self.env.is_enum_variant(name) {
                    return Type::Named(enum_name.to_string());
                }
                // Check for variable
                if let Some(ty) = self.env.lookup(name) {
                    return ty.clone();
                }
                self.errors.push(TypeError::UndefinedVariable {
                    name: name.clone(),
                    span: *span,
                });
                Type::Infer
            }
            Expr::Binary { op, left, right, span } => {
                let left_ty = self.infer_expr(left);
                let right_ty = self.infer_expr(right);

                match op {
                    BinOp::Add | BinOp::Sub | BinOp::Mul | BinOp::Div | BinOp::Mod => {
                        if left_ty.is_numeric() && right_ty.is_numeric() {
                            // Promote to wider type
                            self.promote_numeric(&left_ty, &right_ty)
                        } else {
                            self.errors.push(TypeError::InvalidOperator {
                                op: op.as_str().to_string(),
                                left: format!("{}", left_ty),
                                right: format!("{}", right_ty),
                                span: *span,
                            });
                            Type::Infer
                        }
                    }
                    BinOp::Eq | BinOp::Ne | BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge => {
                        Type::Bool
                    }
                    BinOp::And | BinOp::Or => {
                        if left_ty != Type::Bool || right_ty != Type::Bool {
                            self.errors.push(TypeError::InvalidOperator {
                                op: op.as_str().to_string(),
                                left: format!("{}", left_ty),
                                right: format!("{}", right_ty),
                                span: *span,
                            });
                        }
                        Type::Bool
                    }
                    BinOp::BitAnd | BinOp::BitOr | BinOp::BitXor | BinOp::Shl | BinOp::Shr => {
                        if left_ty.is_integer() && right_ty.is_integer() {
                            self.promote_numeric(&left_ty, &right_ty)
                        } else {
                            self.errors.push(TypeError::InvalidOperator {
                                op: op.as_str().to_string(),
                                left: format!("{}", left_ty),
                                right: format!("{}", right_ty),
                                span: *span,
                            });
                            Type::Infer
                        }
                    }
                }
            }
            Expr::Unary { op, expr: inner, span } => {
                let inner_ty = self.infer_expr(inner);
                match op {
                    UnOp::Not => {
                        if inner_ty != Type::Bool {
                            self.errors.push(TypeError::TypeMismatch {
                                expected: "bool".to_string(),
                                found: format!("{}", inner_ty),
                                span: *span,
                            });
                        }
                        Type::Bool
                    }
                    UnOp::Neg => {
                        if !inner_ty.is_numeric() {
                            self.errors.push(TypeError::TypeMismatch {
                                expected: "numeric".to_string(),
                                found: format!("{}", inner_ty),
                                span: *span,
                            });
                        }
                        inner_ty
                    }
                    UnOp::BitNot => {
                        if !inner_ty.is_integer() {
                            self.errors.push(TypeError::TypeMismatch {
                                expected: "integer".to_string(),
                                found: format!("{}", inner_ty),
                                span: *span,
                            });
                        }
                        inner_ty
                    }
                }
            }
            Expr::Call { func, args, span } => {
                if let Some(fn_type) = self.env.get_function(func).cloned() {
                    if args.len() != fn_type.params.len() {
                        self.errors.push(TypeError::ArityMismatch {
                            expected: fn_type.params.len(),
                            found: args.len(),
                            span: *span,
                        });
                    }
                    fn_type.return_type
                } else {
                    // Built-in functions handled elsewhere
                    Type::Infer
                }
            }
            Expr::MethodCall { receiver, method, .. } => {
                let _receiver_ty = self.infer_expr(receiver);
                // Method resolution would go here
                Type::Infer
            }
            Expr::FieldAccess { expr: inner, field, span } => {
                let inner_ty = self.infer_expr(inner);
                if let Type::Named(struct_name) = &inner_ty {
                    if let Some(struct_type) = self.env.get_struct(struct_name).cloned() {
                        for (name, ty) in &struct_type.fields {
                            if name == field {
                                return ty.clone();
                            }
                        }
                        self.errors.push(TypeError::FieldNotFound {
                            field: field.clone(),
                            struct_name: struct_name.clone(),
                            span: *span,
                        });
                    }
                }
                Type::Infer
            }
            Expr::Index { expr: inner, index, .. } => {
                let inner_ty = self.infer_expr(inner);
                let _index_ty = self.infer_expr(index);

                if let Type::Array { elem, .. } = inner_ty {
                    *elem
                } else {
                    Type::Infer
                }
            }
            Expr::StructInit { name, fields, span } => {
                if let Some(struct_type) = self.env.get_struct(name).cloned() {
                    for (field_name, field_expr) in fields {
                        let field_ty = self.infer_expr(field_expr);
                        let expected = struct_type
                            .fields
                            .iter()
                            .find(|(n, _)| n == field_name)
                            .map(|(_, t)| t);
                        if let Some(expected_ty) = expected {
                            if !self.types_compatible(expected_ty, &field_ty) {
                                self.errors.push(TypeError::TypeMismatch {
                                    expected: format!("{}", expected_ty),
                                    found: format!("{}", field_ty),
                                    span: *span,
                                });
                            }
                        }
                    }
                    Type::Named(name.clone())
                } else {
                    self.errors.push(TypeError::UndefinedType {
                        name: name.clone(),
                        span: *span,
                    });
                    Type::Infer
                }
            }
            Expr::ArrayInit { elements, .. } => {
                if elements.is_empty() {
                    Type::Array {
                        elem: Box::new(Type::Infer),
                        size: 0,
                    }
                } else {
                    let elem_ty = self.infer_expr(&elements[0]);
                    Type::Array {
                        elem: Box::new(elem_ty),
                        size: elements.len(),
                    }
                }
            }
            Expr::Cast { ty, .. } => ty.clone(),
            Expr::IfExpr { cond, then_expr, else_expr, span } => {
                let cond_ty = self.infer_expr(cond);
                if cond_ty != Type::Bool {
                    self.errors.push(TypeError::TypeMismatch {
                        expected: "bool".to_string(),
                        found: format!("{}", cond_ty),
                        span: *span,
                    });
                }
                let then_ty = self.infer_expr(then_expr);
                let else_ty = self.infer_expr(else_expr);
                if !self.types_compatible(&then_ty, &else_ty) {
                    self.errors.push(TypeError::TypeMismatch {
                        expected: format!("{}", then_ty),
                        found: format!("{}", else_ty),
                        span: *span,
                    });
                }
                then_ty
            }
            Expr::Block { block, .. } => {
                // Last expression in block is the result
                if let Some(Stmt::Expr { expr, .. }) = block.stmts.last() {
                    self.infer_expr(expr)
                } else {
                    Type::Unit
                }
            }
        }
    }

    fn infer_assign_target(&mut self, target: &ast::AssignTarget) -> Type {
        match target {
            ast::AssignTarget::Ident(name) => {
                self.env.lookup(name).cloned().unwrap_or(Type::Infer)
            }
            ast::AssignTarget::Field { base, field } => {
                let base_ty = self.infer_assign_target(base);
                if let Type::Named(struct_name) = &base_ty {
                    if let Some(struct_type) = self.env.get_struct(struct_name).cloned() {
                        for (name, ty) in &struct_type.fields {
                            if name == field {
                                return ty.clone();
                            }
                        }
                    }
                }
                Type::Infer
            }
            ast::AssignTarget::Index { base, .. } => {
                let base_ty = self.infer_assign_target(base);
                if let Type::Array { elem, .. } = base_ty {
                    *elem
                } else {
                    Type::Infer
                }
            }
        }
    }

    fn types_compatible(&self, expected: &Type, found: &Type) -> bool {
        if expected == found {
            return true;
        }
        // Allow Infer to match anything
        if matches!(expected, Type::Infer) || matches!(found, Type::Infer) {
            return true;
        }
        // Allow integer type coercion
        if expected.is_integer() && found.is_integer() {
            return true;
        }
        // Allow numeric type coercion for fixed-point
        if expected.is_numeric() && found.is_numeric() {
            return true;
        }
        false
    }

    fn promote_numeric(&self, left: &Type, right: &Type) -> Type {
        // Q16.16 takes precedence
        if matches!(left, Type::Q16_16) || matches!(right, Type::Q16_16) {
            return Type::Q16_16;
        }
        // Otherwise, prefer larger integer types
        match (left, right) {
            (Type::I64, _) | (_, Type::I64) => Type::I64,
            (Type::U64, _) | (_, Type::U64) => Type::U64,
            (Type::I32, _) | (_, Type::I32) => Type::I32,
            (Type::U32, _) | (_, Type::U32) => Type::U32,
            (Type::I16, _) | (_, Type::I16) => Type::I16,
            (Type::U16, _) | (_, Type::U16) => Type::U16,
            (Type::I8, _) | (_, Type::I8) => Type::I8,
            (Type::U8, _) | (_, Type::U8) => Type::U8,
            _ => left.clone(),
        }
    }
}

impl Default for TypeChecker {
    fn default() -> Self {
        Self::new()
    }
}

/// Type check a program
pub fn type_check(program: &Program) -> Result<TypeEnv, Vec<TypeError>> {
    let mut checker = TypeChecker::new();
    checker.check(program)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse;

    #[test]
    fn test_type_check_simple() {
        let program = parse("fn add(a: i32, b: i32) -> i32 { return a + b }").unwrap();
        let result = type_check(&program);
        assert!(result.is_ok());
    }

    #[test]
    fn test_type_check_fixed() {
        let program = parse(
            "fn gradient(my_load: q16_16, neighbor_load: q16_16) -> q16_16 { return neighbor_load - my_load }",
        )
        .unwrap();
        let result = type_check(&program);
        assert!(result.is_ok());
    }

    #[test]
    fn test_builtin_enums() {
        let env = TypeEnv::new();
        assert!(env.get_enum("Error").is_some());
        assert!(env.get_enum("HealthState").is_some());
        assert!(env.is_enum_variant("Error::Ok").is_some());
    }
}
