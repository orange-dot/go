//! EKKL Evaluator
//!
//! AST interpreter for EKKL programs.

use std::collections::HashMap;

use crate::ast::*;
use crate::builtins::Builtins;
use crate::error::EvalError;
use crate::types::TypeEnv;
use crate::value::{Fixed, Value};

/// Maximum call stack depth
const MAX_STACK_DEPTH: usize = 256;

/// Stack frame for function calls
#[derive(Debug, Clone)]
struct StackFrame {
    /// Local variables
    locals: HashMap<String, Value>,
    /// Function name (for debugging)
    func_name: String,
}

impl StackFrame {
    fn new(func_name: &str) -> Self {
        Self {
            locals: HashMap::new(),
            func_name: func_name.to_string(),
        }
    }
}

/// EKKL evaluator
pub struct Evaluator {
    /// Type environment
    type_env: TypeEnv,
    /// Function definitions
    functions: HashMap<String, FnDef>,
    /// Global constants
    globals: HashMap<String, Value>,
    /// Call stack
    call_stack: Vec<StackFrame>,
    /// Built-in functions
    builtins: Builtins,
}

impl Evaluator {
    /// Create a new evaluator
    pub fn new() -> Self {
        let mut eval = Self {
            type_env: TypeEnv::new(),
            functions: HashMap::new(),
            globals: HashMap::new(),
            call_stack: Vec::new(),
            builtins: Builtins::new(),
        };

        // Initialize built-in constants
        eval.init_constants();

        eval
    }

    fn init_constants(&mut self) {
        // EKK configuration constants (matching ekk::types)
        self.globals.insert("K_NEIGHBORS".to_string(), Value::U8(7));
        self.globals.insert("MAX_MODULES".to_string(), Value::U16(256));
        self.globals.insert("MAX_TASKS_PER_MODULE".to_string(), Value::U8(8));
        self.globals.insert("FIELD_DECAY_TAU_US".to_string(), Value::U64(100_000));
        self.globals.insert("HEARTBEAT_PERIOD_US".to_string(), Value::U64(10_000));
        self.globals.insert("HEARTBEAT_TIMEOUT_COUNT".to_string(), Value::U8(5));
        self.globals.insert("VOTE_TIMEOUT_US".to_string(), Value::U64(50_000));
        self.globals.insert("MAX_BALLOTS".to_string(), Value::U8(4));
        self.globals.insert("FIELD_COUNT".to_string(), Value::U8(5));
        self.globals.insert("INVALID_MODULE_ID".to_string(), Value::U8(0));
        self.globals.insert("INVALID_BALLOT_ID".to_string(), Value::U16(0));
        self.globals.insert("BROADCAST_ID".to_string(), Value::U8(255));

        // Threshold constants
        self.globals.insert("SIMPLE_MAJORITY".to_string(), Value::Fixed(Fixed::from_bits(0x8000))); // 0.5
        self.globals.insert("SUPERMAJORITY".to_string(), Value::Fixed(Fixed::from_bits(0xAB85))); // ~0.67
        self.globals.insert("UNANIMOUS".to_string(), Value::Fixed(Fixed::ONE));
    }

    /// Load a program into the evaluator
    pub fn load_program(&mut self, program: &Program) -> Result<(), EvalError> {
        for item in &program.items {
            match item {
                Item::FnDef(f) => {
                    self.functions.insert(f.name.clone(), f.clone());
                }
                Item::ConstDef(c) => {
                    let value = self.eval_expr(&c.value)?;
                    self.globals.insert(c.name.clone(), value);
                }
                Item::StructDef(_) | Item::EnumDef(_) => {
                    // Types are handled by TypeEnv
                }
            }
        }
        Ok(())
    }

    /// Check if a function exists
    pub fn has_function(&self, name: &str) -> bool {
        self.functions.contains_key(name) || self.builtins.has(name)
    }

    /// Call a function by name
    pub fn call(&mut self, name: &str, args: Vec<Value>) -> Result<Value, EvalError> {
        // Check for built-in function first
        if self.builtins.has(name) {
            return self.builtins.call(name, args);
        }

        let func = self
            .functions
            .get(name)
            .cloned()
            .ok_or_else(|| EvalError::UndefinedFunction(name.to_string()))?;

        if self.call_stack.len() >= MAX_STACK_DEPTH {
            return Err(EvalError::StackOverflow);
        }

        // Create new stack frame
        let mut frame = StackFrame::new(name);

        // Bind parameters
        for (param, value) in func.params.iter().zip(args) {
            frame.locals.insert(param.name.clone(), value);
        }

        self.call_stack.push(frame);

        // Execute function body
        let result = self.eval_block(&func.body);

        self.call_stack.pop();

        match result {
            Ok(v) => Ok(v),
            Err(EvalError::Return(v)) => Ok(*v),
            Err(e) => Err(e),
        }
    }

    fn eval_block(&mut self, block: &Block) -> Result<Value, EvalError> {
        let mut result = Value::Unit;

        for stmt in &block.stmts {
            result = self.eval_stmt(stmt)?;
        }

        Ok(result)
    }

    fn eval_stmt(&mut self, stmt: &Stmt) -> Result<Value, EvalError> {
        match stmt {
            Stmt::Let { name, init, .. } => {
                let value = self.eval_expr(init)?;
                self.set_local(name, value);
                Ok(Value::Unit)
            }
            Stmt::Assign { target, value, .. } => {
                let value = self.eval_expr(value)?;
                self.assign_target(target, value)?;
                Ok(Value::Unit)
            }
            Stmt::Return { value, .. } => {
                let result = match value {
                    Some(expr) => self.eval_expr(expr)?,
                    None => Value::Unit,
                };
                Err(EvalError::Return(Box::new(result)))
            }
            Stmt::If { cond, then_block, else_block, .. } => {
                let cond_val = self.eval_expr(cond)?;
                if cond_val.as_bool()? {
                    self.eval_block(then_block)
                } else if let Some(else_block) = else_block {
                    self.eval_block(else_block)
                } else {
                    Ok(Value::Unit)
                }
            }
            Stmt::Match { expr, arms, .. } => {
                let value = self.eval_expr(expr)?;
                for arm in arms {
                    if self.pattern_matches(&arm.pattern, &value) {
                        return self.eval_expr(&arm.body);
                    }
                }
                // No match - return unit
                Ok(Value::Unit)
            }
            Stmt::For { var, start, end, body, .. } => {
                let start_val = self.eval_expr(start)?.as_i64()?;
                let end_val = self.eval_expr(end)?.as_i64()?;

                for i in start_val..end_val {
                    self.set_local(var, Value::I64(i));
                    match self.eval_block(body) {
                        Ok(_) => {}
                        Err(EvalError::Return(v)) => return Err(EvalError::Return(v)),
                        Err(e) => return Err(e),
                    }
                }
                Ok(Value::Unit)
            }
            Stmt::While { cond, body, .. } => {
                loop {
                    let cond_val = self.eval_expr(cond)?;
                    if !cond_val.as_bool()? {
                        break;
                    }
                    match self.eval_block(body) {
                        Ok(_) => {}
                        Err(EvalError::Return(v)) => return Err(EvalError::Return(v)),
                        Err(e) => return Err(e),
                    }
                }
                Ok(Value::Unit)
            }
            Stmt::Expr { expr, .. } => self.eval_expr(expr),
        }
    }

    fn eval_expr(&mut self, expr: &Expr) -> Result<Value, EvalError> {
        match expr {
            Expr::Literal { value, .. } => self.eval_literal(value),
            Expr::Ident { name, .. } => self.get_var(name),
            Expr::Binary { op, left, right, .. } => {
                let left_val = self.eval_expr(left)?;
                let right_val = self.eval_expr(right)?;
                self.eval_binary_op(*op, left_val, right_val)
            }
            Expr::Unary { op, expr: inner, .. } => {
                let val = self.eval_expr(inner)?;
                self.eval_unary_op(*op, val)
            }
            Expr::Call { func, args, .. } => {
                let arg_values: Vec<Value> = args
                    .iter()
                    .map(|a| self.eval_expr(a))
                    .collect::<Result<_, _>>()?;
                self.call(func, arg_values)
            }
            Expr::MethodCall { receiver, method, args, .. } => {
                let receiver_val = self.eval_expr(receiver)?;
                let mut arg_values: Vec<Value> = vec![receiver_val];
                for a in args {
                    arg_values.push(self.eval_expr(a)?);
                }
                // Try to call as built-in method
                self.builtins.call_method(method, arg_values)
            }
            Expr::FieldAccess { expr: inner, field, .. } => {
                let struct_val = self.eval_expr(inner)?;
                struct_val.get_field(field).cloned()
            }
            Expr::Index { expr: inner, index, .. } => {
                let array_val = self.eval_expr(inner)?;
                let idx = self.eval_expr(index)?.as_i64()? as usize;
                let arr = array_val.as_array()?;
                if idx >= arr.len() {
                    return Err(EvalError::IndexOutOfBounds { index: idx, len: arr.len() });
                }
                Ok(arr[idx].clone())
            }
            Expr::StructInit { name, fields, .. } => {
                let mut field_values = HashMap::new();
                for (field_name, field_expr) in fields {
                    let value = self.eval_expr(field_expr)?;
                    field_values.insert(field_name.clone(), value);
                }
                Ok(Value::Struct {
                    name: name.clone(),
                    fields: field_values,
                })
            }
            Expr::ArrayInit { elements, .. } => {
                let values: Vec<Value> = elements
                    .iter()
                    .map(|e| self.eval_expr(e))
                    .collect::<Result<_, _>>()?;
                Ok(Value::Array(values))
            }
            Expr::Cast { expr: inner, ty, .. } => {
                let val = self.eval_expr(inner)?;
                self.cast_value(val, ty)
            }
            Expr::IfExpr { cond, then_expr, else_expr, .. } => {
                let cond_val = self.eval_expr(cond)?;
                if cond_val.as_bool()? {
                    self.eval_expr(then_expr)
                } else {
                    self.eval_expr(else_expr)
                }
            }
            Expr::Block { block, .. } => self.eval_block(block),
        }
    }

    fn eval_literal(&self, lit: &Literal) -> Result<Value, EvalError> {
        match lit {
            Literal::Int(n) => Ok(Value::I64(*n)),
            Literal::Fixed(bits) => Ok(Value::Fixed(Fixed::from_bits(*bits))),
            Literal::Bool(b) => Ok(Value::Bool(*b)),
            Literal::String(s) => Ok(Value::String(s.clone())),
            Literal::Unit => Ok(Value::Unit),
        }
    }

    fn eval_binary_op(&self, op: BinOp, left: Value, right: Value) -> Result<Value, EvalError> {
        // Handle fixed-point operations
        if matches!(&left, Value::Fixed(_)) || matches!(&right, Value::Fixed(_)) {
            let l = left.as_fixed()?;
            let r = right.as_fixed()?;

            return match op {
                BinOp::Add => Ok(Value::Fixed(l + r)),
                BinOp::Sub => Ok(Value::Fixed(l - r)),
                BinOp::Mul => Ok(Value::Fixed(l * r)),
                BinOp::Div => {
                    if r == Fixed::ZERO {
                        return Err(EvalError::DivisionByZero);
                    }
                    Ok(Value::Fixed(l / r))
                }
                BinOp::Eq => Ok(Value::Bool(l == r)),
                BinOp::Ne => Ok(Value::Bool(l != r)),
                BinOp::Lt => Ok(Value::Bool(l < r)),
                BinOp::Le => Ok(Value::Bool(l <= r)),
                BinOp::Gt => Ok(Value::Bool(l > r)),
                BinOp::Ge => Ok(Value::Bool(l >= r)),
                _ => Err(EvalError::TypeError(format!(
                    "invalid operator {:?} for fixed-point",
                    op
                ))),
            };
        }

        // Handle integer operations
        if left.is_integer() && right.is_integer() {
            let l = left.as_i64()?;
            let r = right.as_i64()?;

            return match op {
                BinOp::Add => Ok(Value::I64(l.wrapping_add(r))),
                BinOp::Sub => Ok(Value::I64(l.wrapping_sub(r))),
                BinOp::Mul => Ok(Value::I64(l.wrapping_mul(r))),
                BinOp::Div => {
                    if r == 0 {
                        return Err(EvalError::DivisionByZero);
                    }
                    Ok(Value::I64(l / r))
                }
                BinOp::Mod => {
                    if r == 0 {
                        return Err(EvalError::DivisionByZero);
                    }
                    Ok(Value::I64(l % r))
                }
                BinOp::Eq => Ok(Value::Bool(l == r)),
                BinOp::Ne => Ok(Value::Bool(l != r)),
                BinOp::Lt => Ok(Value::Bool(l < r)),
                BinOp::Le => Ok(Value::Bool(l <= r)),
                BinOp::Gt => Ok(Value::Bool(l > r)),
                BinOp::Ge => Ok(Value::Bool(l >= r)),
                BinOp::BitAnd => Ok(Value::I64(l & r)),
                BinOp::BitOr => Ok(Value::I64(l | r)),
                BinOp::BitXor => Ok(Value::I64(l ^ r)),
                BinOp::Shl => Ok(Value::I64(l << r)),
                BinOp::Shr => Ok(Value::I64(l >> r)),
                BinOp::And | BinOp::Or => Err(EvalError::TypeError(
                    "logical operators require bool".to_string(),
                )),
            };
        }

        // Handle boolean operations
        if let (Value::Bool(l), Value::Bool(r)) = (&left, &right) {
            return match op {
                BinOp::And => Ok(Value::Bool(*l && *r)),
                BinOp::Or => Ok(Value::Bool(*l || *r)),
                BinOp::Eq => Ok(Value::Bool(l == r)),
                BinOp::Ne => Ok(Value::Bool(l != r)),
                _ => Err(EvalError::TypeError(format!(
                    "invalid operator {:?} for bool",
                    op
                ))),
            };
        }

        // Handle enum comparison
        if let (Value::Enum { .. }, Value::Enum { .. }) = (&left, &right) {
            return match op {
                BinOp::Eq => Ok(Value::Bool(left == right)),
                BinOp::Ne => Ok(Value::Bool(left != right)),
                _ => Err(EvalError::TypeError(format!(
                    "invalid operator {:?} for enum",
                    op
                ))),
            };
        }

        Err(EvalError::TypeError(format!(
            "cannot apply {:?} to {} and {}",
            op,
            left.type_name(),
            right.type_name()
        )))
    }

    fn eval_unary_op(&self, op: UnOp, val: Value) -> Result<Value, EvalError> {
        match op {
            UnOp::Not => {
                let b = val.as_bool()?;
                Ok(Value::Bool(!b))
            }
            UnOp::Neg => {
                if let Value::Fixed(f) = val {
                    Ok(Value::Fixed(-f))
                } else {
                    let n = val.as_i64()?;
                    Ok(Value::I64(-n))
                }
            }
            UnOp::BitNot => {
                let n = val.as_i64()?;
                Ok(Value::I64(!n))
            }
        }
    }

    fn cast_value(&self, val: Value, ty: &Type) -> Result<Value, EvalError> {
        match ty {
            Type::I8 => Ok(Value::I8(val.as_i8()?)),
            Type::I16 => Ok(Value::I16(val.as_i16()?)),
            Type::I32 => Ok(Value::I32(val.as_i32()?)),
            Type::I64 => Ok(Value::I64(val.as_i64()?)),
            Type::U8 => Ok(Value::U8(val.as_u8()?)),
            Type::U16 => Ok(Value::U16(val.as_u16()?)),
            Type::U32 => Ok(Value::U32(val.as_u32()?)),
            Type::U64 => Ok(Value::U64(val.as_u64()?)),
            Type::Q16_16 => {
                if let Value::Fixed(f) = val {
                    Ok(Value::Fixed(f))
                } else if val.is_integer() {
                    Ok(Value::Fixed(Fixed::from_num(val.as_i32()?)))
                } else {
                    Err(EvalError::InvalidCast {
                        from: val.type_name().to_string(),
                        to: "q16_16".to_string(),
                    })
                }
            }
            Type::Bool => {
                if let Value::Bool(b) = val {
                    Ok(Value::Bool(b))
                } else {
                    Err(EvalError::InvalidCast {
                        from: val.type_name().to_string(),
                        to: "bool".to_string(),
                    })
                }
            }
            _ => Err(EvalError::InvalidCast {
                from: val.type_name().to_string(),
                to: format!("{}", ty),
            }),
        }
    }

    fn pattern_matches(&self, pattern: &Pattern, value: &Value) -> bool {
        match pattern {
            Pattern::Wildcard => true,
            Pattern::Ident(_) => true, // Binds to any value
            Pattern::Variant { enum_name, variant } => {
                if let Value::Enum { enum_name: en, variant: v } = value {
                    if let Some(expected_enum) = enum_name {
                        expected_enum == en && variant == v
                    } else {
                        variant == v
                    }
                } else {
                    false
                }
            }
            Pattern::Literal(lit) => {
                match (lit, value) {
                    (Literal::Int(a), Value::I64(b)) => *a == *b,
                    (Literal::Fixed(a), Value::Fixed(b)) => Fixed::from_bits(*a) == *b,
                    (Literal::Bool(a), Value::Bool(b)) => a == b,
                    (Literal::String(a), Value::String(b)) => a == b,
                    _ => false,
                }
            }
        }
    }

    fn get_var(&self, name: &str) -> Result<Value, EvalError> {
        // Check for enum variant
        if let Some(idx) = name.find("::") {
            let enum_name = &name[..idx];
            let variant = &name[idx + 2..];
            return Ok(Value::Enum {
                enum_name: enum_name.to_string(),
                variant: variant.to_string(),
            });
        }

        // Check local variables
        if let Some(frame) = self.call_stack.last() {
            if let Some(val) = frame.locals.get(name) {
                return Ok(val.clone());
            }
        }

        // Check globals
        if let Some(val) = self.globals.get(name) {
            return Ok(val.clone());
        }

        // Check for unqualified enum variant
        if let Some((enum_name, _)) = self.type_env.is_enum_variant(name) {
            return Ok(Value::Enum {
                enum_name: enum_name.to_string(),
                variant: name.to_string(),
            });
        }

        Err(EvalError::UndefinedVariable(name.to_string()))
    }

    fn set_local(&mut self, name: &str, value: Value) {
        if let Some(frame) = self.call_stack.last_mut() {
            frame.locals.insert(name.to_string(), value);
        }
    }

    fn assign_target(&mut self, target: &AssignTarget, value: Value) -> Result<(), EvalError> {
        match target {
            AssignTarget::Ident(name) => {
                self.set_local(name, value);
                Ok(())
            }
            AssignTarget::Field { base, field } => {
                // Get the struct, modify it, and put it back
                let base_val = self.get_assign_target_value(base)?;
                let mut struct_val = base_val;
                struct_val.set_field(field, value)?;
                self.assign_base_target(base, struct_val)
            }
            AssignTarget::Index { base, index } => {
                let idx = self.eval_expr(index)?.as_i64()? as usize;
                let base_val = self.get_assign_target_value(base)?;
                let mut array_val = base_val;
                let arr = array_val.as_array_mut()?;
                if idx >= arr.len() {
                    return Err(EvalError::IndexOutOfBounds { index: idx, len: arr.len() });
                }
                arr[idx] = value;
                self.assign_base_target(base, array_val)
            }
        }
    }

    fn get_assign_target_value(&mut self, target: &AssignTarget) -> Result<Value, EvalError> {
        match target {
            AssignTarget::Ident(name) => self.get_var(name),
            AssignTarget::Field { base, field } => {
                let base_val = self.get_assign_target_value(base)?;
                base_val.get_field(field).cloned()
            }
            AssignTarget::Index { base, index } => {
                let base_val = self.get_assign_target_value(base)?;
                let index_clone = index.clone();
                let idx = self.eval_expr(&index_clone)?.as_i64()? as usize;
                let arr = base_val.as_array()?;
                if idx >= arr.len() {
                    return Err(EvalError::IndexOutOfBounds { index: idx, len: arr.len() });
                }
                Ok(arr[idx].clone())
            }
        }
    }

    fn assign_base_target(&mut self, target: &AssignTarget, value: Value) -> Result<(), EvalError> {
        match target {
            AssignTarget::Ident(name) => {
                self.set_local(name, value);
                Ok(())
            }
            AssignTarget::Field { base, field } => {
                let mut base_val = self.get_assign_target_value(base)?;
                base_val.set_field(field, value)?;
                self.assign_base_target(base, base_val)
            }
            AssignTarget::Index { base, index } => {
                let idx = self.eval_expr(index)?.as_i64()? as usize;
                let mut base_val = self.get_assign_target_value(base)?;
                let arr = base_val.as_array_mut()?;
                if idx >= arr.len() {
                    return Err(EvalError::IndexOutOfBounds { index: idx, len: arr.len() });
                }
                arr[idx] = value;
                self.assign_base_target(base, base_val)
            }
        }
    }
}

impl Default for Evaluator {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse;

    #[test]
    fn test_simple_function() {
        let program = parse("fn add(a: i32, b: i32) -> i32 { return a + b }").unwrap();
        let mut eval = Evaluator::new();
        eval.load_program(&program).unwrap();

        let result = eval.call("add", vec![Value::I32(2), Value::I32(3)]).unwrap();
        assert_eq!(result, Value::I64(5)); // Returns i64 due to promotion
    }

    #[test]
    fn test_fixed_point() {
        let program = parse(
            "fn grad(a: q16_16, b: q16_16) -> q16_16 { return b - a }",
        )
        .unwrap();
        let mut eval = Evaluator::new();
        eval.load_program(&program).unwrap();

        let result = eval
            .call(
                "grad",
                vec![
                    Value::Fixed(Fixed::from_num(0.3)),
                    Value::Fixed(Fixed::from_num(0.7)),
                ],
            )
            .unwrap();

        if let Value::Fixed(f) = result {
            let expected = Fixed::from_num(0.4);
            let diff = (f - expected).abs();
            assert!(diff < Fixed::from_num(0.001));
        } else {
            panic!("Expected Fixed");
        }
    }

    #[test]
    fn test_conditionals() {
        let program = parse(
            r#"
            fn max(a: i32, b: i32) -> i32 {
                if a > b {
                    return a
                } else {
                    return b
                }
            }
            "#,
        )
        .unwrap();
        let mut eval = Evaluator::new();
        eval.load_program(&program).unwrap();

        let result = eval.call("max", vec![Value::I32(5), Value::I32(3)]).unwrap();
        assert_eq!(result.as_i64().unwrap(), 5);
    }

    #[test]
    fn test_loops() {
        let program = parse(
            r#"
            fn sum(n: i32) -> i32 {
                let mut total: i32 = 0
                for i in 0..n {
                    total = total + i
                }
                return total
            }
            "#,
        )
        .unwrap();
        let mut eval = Evaluator::new();
        eval.load_program(&program).unwrap();

        let result = eval.call("sum", vec![Value::I32(5)]).unwrap();
        assert_eq!(result.as_i64().unwrap(), 10); // 0+1+2+3+4 = 10
    }

    #[test]
    fn test_builtin_constants() {
        let eval = Evaluator::new();
        let k = eval.globals.get("K_NEIGHBORS").unwrap();
        assert_eq!(k.as_u8().unwrap(), 7);
    }
}
