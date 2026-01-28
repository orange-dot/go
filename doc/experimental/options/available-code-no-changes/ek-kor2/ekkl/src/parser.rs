//! EKKL Parser
//!
//! Recursive descent parser that converts tokens to AST.

use crate::ast::*;
use crate::error::{ParseError, Span};
use crate::lexer::{SpannedToken, Token};

/// Parser state
pub struct Parser {
    tokens: Vec<SpannedToken>,
    pos: usize,
}

impl Parser {
    pub fn new(tokens: Vec<SpannedToken>) -> Self {
        Self { tokens, pos: 0 }
    }

    /// Parse a complete program
    pub fn parse_program(&mut self) -> Result<Program, ParseError> {
        let mut items = Vec::new();

        while !self.is_at_end() {
            items.push(self.parse_item()?);
        }

        Ok(Program { items })
    }

    // ========================================================================
    // Helper methods
    // ========================================================================

    fn is_at_end(&self) -> bool {
        matches!(self.peek().token, Token::Eof)
    }

    fn peek(&self) -> &SpannedToken {
        &self.tokens[self.pos]
    }

    fn peek_token(&self) -> &Token {
        &self.tokens[self.pos].token
    }

    fn peek_ahead(&self, n: usize) -> Option<&SpannedToken> {
        self.tokens.get(self.pos + n)
    }

    /// Check if the current position looks like the start of a struct initialization.
    /// This looks ahead after `{` to see if we have `Ident :` pattern.
    /// Called when we're at `{` token.
    fn looks_like_struct_init(&self) -> bool {
        // We're at `{`, look at what follows:
        // Struct init: `{ field: value, ... }` or `{ }`
        // Block: `{ statement... }` where statement could be `let`, `return`, `if`, etc.

        // Look at token after `{`
        if let Some(after_brace) = self.peek_ahead(1) {
            match &after_brace.token {
                // Empty struct is struct init
                Token::RBrace => true,
                // Identifier followed by colon is struct init
                Token::Ident(_) => {
                    if let Some(after_ident) = self.peek_ahead(2) {
                        matches!(after_ident.token, Token::Colon)
                    } else {
                        false
                    }
                }
                // Anything else (keywords, operators) is not struct init
                _ => false,
            }
        } else {
            false
        }
    }

    fn advance(&mut self) -> &SpannedToken {
        let token = &self.tokens[self.pos];
        if !self.is_at_end() {
            self.pos += 1;
        }
        token
    }

    fn check(&self, token: &Token) -> bool {
        std::mem::discriminant(self.peek_token()) == std::mem::discriminant(token)
    }

    fn check_any(&self, tokens: &[Token]) -> bool {
        tokens.iter().any(|t| self.check(t))
    }

    fn consume(&mut self, expected: &Token, msg: &str) -> Result<&SpannedToken, ParseError> {
        if self.check(expected) {
            Ok(self.advance())
        } else {
            Err(ParseError::UnexpectedToken {
                expected: msg.to_string(),
                found: format!("{:?}", self.peek_token()),
                span: self.peek().span,
            })
        }
    }

    fn current_span(&self) -> Span {
        self.peek().span
    }

    // ========================================================================
    // Item parsing
    // ========================================================================

    fn parse_item(&mut self) -> Result<Item, ParseError> {
        match self.peek_token() {
            Token::Fn => Ok(Item::FnDef(self.parse_fn_def()?)),
            Token::Struct => Ok(Item::StructDef(self.parse_struct_def()?)),
            Token::Enum => Ok(Item::EnumDef(self.parse_enum_def()?)),
            Token::Const => Ok(Item::ConstDef(self.parse_const_def()?)),
            _ => Err(ParseError::UnexpectedToken {
                expected: "fn, struct, enum, or const".to_string(),
                found: format!("{:?}", self.peek_token()),
                span: self.current_span(),
            }),
        }
    }

    fn parse_fn_def(&mut self) -> Result<FnDef, ParseError> {
        let start = self.current_span();
        self.consume(&Token::Fn, "fn")?;

        let name = self.parse_ident()?;
        self.consume(&Token::LParen, "(")?;

        let mut params = Vec::new();
        while !self.check(&Token::RParen) {
            if !params.is_empty() {
                self.consume(&Token::Comma, ",")?;
            }
            if self.check(&Token::RParen) {
                break;
            }
            params.push(self.parse_param()?);
        }
        self.consume(&Token::RParen, ")")?;

        let return_type = if self.check(&Token::Arrow) {
            self.advance();
            Some(self.parse_type()?)
        } else {
            None
        };

        let body = self.parse_block()?;

        Ok(FnDef {
            name,
            params,
            return_type,
            body,
            span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
        })
    }

    fn parse_param(&mut self) -> Result<Param, ParseError> {
        let start = self.current_span();
        let name = self.parse_ident()?;
        self.consume(&Token::Colon, ":")?;
        let ty = self.parse_type()?;

        Ok(Param {
            name,
            ty,
            span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
        })
    }

    fn parse_struct_def(&mut self) -> Result<StructDef, ParseError> {
        let start = self.current_span();
        self.consume(&Token::Struct, "struct")?;
        let name = self.parse_ident()?;
        self.consume(&Token::LBrace, "{")?;

        let mut fields = Vec::new();
        while !self.check(&Token::RBrace) {
            fields.push(self.parse_struct_field()?);
            if !self.check(&Token::RBrace) {
                self.consume(&Token::Comma, ",")?;
            }
        }
        self.consume(&Token::RBrace, "}")?;

        Ok(StructDef {
            name,
            fields,
            span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
        })
    }

    fn parse_struct_field(&mut self) -> Result<StructField, ParseError> {
        let start = self.current_span();
        let name = self.parse_ident()?;
        self.consume(&Token::Colon, ":")?;
        let ty = self.parse_type()?;

        Ok(StructField {
            name,
            ty,
            span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
        })
    }

    fn parse_enum_def(&mut self) -> Result<EnumDef, ParseError> {
        let start = self.current_span();
        self.consume(&Token::Enum, "enum")?;
        let name = self.parse_ident()?;
        self.consume(&Token::LBrace, "{")?;

        let mut variants = Vec::new();
        while !self.check(&Token::RBrace) {
            variants.push(self.parse_ident()?);
            if !self.check(&Token::RBrace) {
                self.consume(&Token::Comma, ",")?;
            }
        }
        self.consume(&Token::RBrace, "}")?;

        Ok(EnumDef {
            name,
            variants,
            span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
        })
    }

    fn parse_const_def(&mut self) -> Result<ConstDef, ParseError> {
        let start = self.current_span();
        self.consume(&Token::Const, "const")?;
        let name = self.parse_ident()?;
        self.consume(&Token::Colon, ":")?;
        let ty = self.parse_type()?;
        self.consume(&Token::Assign, "=")?;
        let value = self.parse_expr()?;

        Ok(ConstDef {
            name,
            ty,
            value,
            span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
        })
    }

    // ========================================================================
    // Type parsing
    // ========================================================================

    fn parse_type(&mut self) -> Result<Type, ParseError> {
        match self.peek_token().clone() {
            Token::I8 => { self.advance(); Ok(Type::I8) }
            Token::I16 => { self.advance(); Ok(Type::I16) }
            Token::I32 => { self.advance(); Ok(Type::I32) }
            Token::I64 => { self.advance(); Ok(Type::I64) }
            Token::U8 => { self.advance(); Ok(Type::U8) }
            Token::U16 => { self.advance(); Ok(Type::U16) }
            Token::U32 => { self.advance(); Ok(Type::U32) }
            Token::U64 => { self.advance(); Ok(Type::U64) }
            Token::Bool => { self.advance(); Ok(Type::Bool) }
            Token::Q16_16 => { self.advance(); Ok(Type::Q16_16) }
            Token::LParen => {
                self.advance();
                self.consume(&Token::RParen, ")")?;
                Ok(Type::Unit)
            }
            Token::LBracket => {
                self.advance();
                let elem = Box::new(self.parse_type()?);
                self.consume(&Token::Semicolon, ";")?;
                let size = self.parse_int_literal()? as usize;
                self.consume(&Token::RBracket, "]")?;
                Ok(Type::Array { elem, size })
            }
            Token::Ident(name) => {
                self.advance();
                // Check for generic types like Result<T, E> or Option<T>
                if self.check(&Token::Lt) {
                    self.advance();
                    if name == "Result" {
                        let ok = Box::new(self.parse_type()?);
                        self.consume(&Token::Comma, ",")?;
                        let err = Box::new(self.parse_type()?);
                        self.consume(&Token::Gt, ">")?;
                        Ok(Type::Result { ok, err })
                    } else if name == "Option" {
                        let inner = Box::new(self.parse_type()?);
                        self.consume(&Token::Gt, ">")?;
                        Ok(Type::Option(inner))
                    } else {
                        // Just consume generic args and return named type
                        self.parse_type()?;
                        while self.check(&Token::Comma) {
                            self.advance();
                            self.parse_type()?;
                        }
                        self.consume(&Token::Gt, ">")?;
                        Ok(Type::Named(name))
                    }
                } else {
                    Ok(Type::Named(name))
                }
            }
            _ => Err(ParseError::InvalidType(self.current_span())),
        }
    }

    // ========================================================================
    // Statement parsing
    // ========================================================================

    fn parse_block(&mut self) -> Result<Block, ParseError> {
        let start = self.current_span();
        self.consume(&Token::LBrace, "{")?;

        let mut stmts = Vec::new();
        while !self.check(&Token::RBrace) {
            stmts.push(self.parse_stmt()?);
        }
        self.consume(&Token::RBrace, "}")?;

        Ok(Block {
            stmts,
            span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
        })
    }

    fn parse_stmt(&mut self) -> Result<Stmt, ParseError> {
        match self.peek_token().clone() {
            Token::Let => self.parse_let_stmt(),
            Token::Return => self.parse_return_stmt(),
            Token::If => self.parse_if_stmt(),
            Token::Match => self.parse_match_stmt(),
            Token::For => self.parse_for_stmt(),
            Token::While => self.parse_while_stmt(),
            _ => self.parse_expr_or_assign_stmt(),
        }
    }

    fn parse_let_stmt(&mut self) -> Result<Stmt, ParseError> {
        let start = self.current_span();
        self.consume(&Token::Let, "let")?;

        let mutable = if self.check(&Token::Mut) {
            self.advance();
            true
        } else {
            false
        };

        let name = self.parse_ident()?;

        let ty = if self.check(&Token::Colon) {
            self.advance();
            Some(self.parse_type()?)
        } else {
            None
        };

        self.consume(&Token::Assign, "=")?;
        let init = self.parse_expr()?;

        Ok(Stmt::Let {
            name,
            mutable,
            ty,
            init,
            span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
        })
    }

    fn parse_return_stmt(&mut self) -> Result<Stmt, ParseError> {
        let start = self.current_span();
        self.consume(&Token::Return, "return")?;

        let value = if self.check(&Token::RBrace) || self.check(&Token::Semicolon) {
            None
        } else {
            Some(self.parse_expr()?)
        };

        Ok(Stmt::Return {
            value,
            span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
        })
    }

    fn parse_if_stmt(&mut self) -> Result<Stmt, ParseError> {
        let start = self.current_span();
        self.consume(&Token::If, "if")?;

        let cond = self.parse_expr()?;
        let then_block = self.parse_block()?;

        let else_block = if self.check(&Token::Else) {
            self.advance();
            if self.check(&Token::If) {
                // else if => wrap in block
                let if_stmt = self.parse_if_stmt()?;
                Some(Block {
                    stmts: vec![if_stmt],
                    span: self.tokens[self.pos - 1].span,
                })
            } else {
                Some(self.parse_block()?)
            }
        } else {
            None
        };

        Ok(Stmt::If {
            cond,
            then_block,
            else_block,
            span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
        })
    }

    fn parse_match_stmt(&mut self) -> Result<Stmt, ParseError> {
        let start = self.current_span();
        self.consume(&Token::Match, "match")?;

        let expr = self.parse_expr()?;
        self.consume(&Token::LBrace, "{")?;

        let mut arms = Vec::new();
        while !self.check(&Token::RBrace) {
            arms.push(self.parse_match_arm()?);
            if !self.check(&Token::RBrace) {
                self.consume(&Token::Comma, ",")?;
            }
        }
        self.consume(&Token::RBrace, "}")?;

        Ok(Stmt::Match {
            expr,
            arms,
            span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
        })
    }

    fn parse_match_arm(&mut self) -> Result<MatchArm, ParseError> {
        let start = self.current_span();
        let pattern = self.parse_pattern()?;
        self.consume(&Token::FatArrow, "=>")?;
        let body = self.parse_expr()?;

        Ok(MatchArm {
            pattern,
            body,
            span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
        })
    }

    fn parse_pattern(&mut self) -> Result<Pattern, ParseError> {
        match self.peek_token().clone() {
            Token::Underscore => {
                self.advance();
                Ok(Pattern::Wildcard)
            }
            Token::True => {
                self.advance();
                Ok(Pattern::Literal(Literal::Bool(true)))
            }
            Token::False => {
                self.advance();
                Ok(Pattern::Literal(Literal::Bool(false)))
            }
            Token::IntLit(n) => {
                self.advance();
                Ok(Pattern::Literal(Literal::Int(n)))
            }
            Token::FixedLit(n) => {
                self.advance();
                Ok(Pattern::Literal(Literal::Fixed(n)))
            }
            Token::StringLit(s) => {
                self.advance();
                Ok(Pattern::Literal(Literal::String(s)))
            }
            Token::Ident(name) => {
                self.advance();
                // Check for Enum::Variant pattern
                if self.check(&Token::ColonColon) {
                    self.advance();
                    let variant = self.parse_ident()?;
                    Ok(Pattern::Variant {
                        enum_name: Some(name),
                        variant,
                    })
                } else {
                    // Could be variant or binding - treat as variant if uppercase, binding otherwise
                    if name.chars().next().map(|c| c.is_uppercase()).unwrap_or(false) {
                        Ok(Pattern::Variant {
                            enum_name: None,
                            variant: name,
                        })
                    } else {
                        Ok(Pattern::Ident(name))
                    }
                }
            }
            _ => Err(ParseError::InvalidExpr(self.current_span())),
        }
    }

    fn parse_for_stmt(&mut self) -> Result<Stmt, ParseError> {
        let start = self.current_span();
        self.consume(&Token::For, "for")?;

        let var = self.parse_ident()?;
        self.consume(&Token::In, "in")?;

        let start_expr = self.parse_expr()?;
        self.consume(&Token::DotDot, "..")?;
        let end_expr = self.parse_expr()?;

        let body = self.parse_block()?;

        Ok(Stmt::For {
            var,
            start: start_expr,
            end: end_expr,
            body,
            span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
        })
    }

    fn parse_while_stmt(&mut self) -> Result<Stmt, ParseError> {
        let start = self.current_span();
        self.consume(&Token::While, "while")?;

        let cond = self.parse_expr()?;
        let body = self.parse_block()?;

        Ok(Stmt::While {
            cond,
            body,
            span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
        })
    }

    fn parse_expr_or_assign_stmt(&mut self) -> Result<Stmt, ParseError> {
        let start = self.current_span();
        let expr = self.parse_expr()?;

        // Check for assignment
        if self.check(&Token::Assign) {
            self.advance();
            let value = self.parse_expr()?;
            let target = self.expr_to_assign_target(expr)?;
            Ok(Stmt::Assign {
                target,
                value,
                span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
            })
        } else {
            Ok(Stmt::Expr {
                expr,
                span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
            })
        }
    }

    fn expr_to_assign_target(&self, expr: Expr) -> Result<AssignTarget, ParseError> {
        match expr {
            Expr::Ident { name, .. } => Ok(AssignTarget::Ident(name)),
            Expr::FieldAccess { expr, field, span } => {
                let base = Box::new(self.expr_to_assign_target(*expr)?);
                Ok(AssignTarget::Field { base, field })
            }
            Expr::Index { expr, index, span } => {
                let base = Box::new(self.expr_to_assign_target(*expr)?);
                Ok(AssignTarget::Index { base, index: *index })
            }
            _ => Err(ParseError::InvalidExpr(expr.span())),
        }
    }

    // ========================================================================
    // Expression parsing (precedence climbing)
    // ========================================================================

    fn parse_expr(&mut self) -> Result<Expr, ParseError> {
        self.parse_or_expr()
    }

    fn parse_or_expr(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_and_expr()?;

        while self.check(&Token::Or) {
            let start = left.span();
            self.advance();
            let right = self.parse_and_expr()?;
            left = Expr::Binary {
                op: BinOp::Or,
                left: Box::new(left),
                right: Box::new(right),
                span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
            };
        }

        Ok(left)
    }

    fn parse_and_expr(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_equality_expr()?;

        while self.check(&Token::And) {
            let start = left.span();
            self.advance();
            let right = self.parse_equality_expr()?;
            left = Expr::Binary {
                op: BinOp::And,
                left: Box::new(left),
                right: Box::new(right),
                span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
            };
        }

        Ok(left)
    }

    fn parse_equality_expr(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_comparison_expr()?;

        loop {
            let op = match self.peek_token() {
                Token::Eq => BinOp::Eq,
                Token::Ne => BinOp::Ne,
                _ => break,
            };
            let start = left.span();
            self.advance();
            let right = self.parse_comparison_expr()?;
            left = Expr::Binary {
                op,
                left: Box::new(left),
                right: Box::new(right),
                span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
            };
        }

        Ok(left)
    }

    fn parse_comparison_expr(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_bitwise_or_expr()?;

        loop {
            let op = match self.peek_token() {
                Token::Lt => BinOp::Lt,
                Token::Le => BinOp::Le,
                Token::Gt => BinOp::Gt,
                Token::Ge => BinOp::Ge,
                _ => break,
            };
            let start = left.span();
            self.advance();
            let right = self.parse_bitwise_or_expr()?;
            left = Expr::Binary {
                op,
                left: Box::new(left),
                right: Box::new(right),
                span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
            };
        }

        Ok(left)
    }

    fn parse_bitwise_or_expr(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_bitwise_xor_expr()?;

        while self.check(&Token::Pipe) {
            let start = left.span();
            self.advance();
            let right = self.parse_bitwise_xor_expr()?;
            left = Expr::Binary {
                op: BinOp::BitOr,
                left: Box::new(left),
                right: Box::new(right),
                span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
            };
        }

        Ok(left)
    }

    fn parse_bitwise_xor_expr(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_bitwise_and_expr()?;

        while self.check(&Token::Caret) {
            let start = left.span();
            self.advance();
            let right = self.parse_bitwise_and_expr()?;
            left = Expr::Binary {
                op: BinOp::BitXor,
                left: Box::new(left),
                right: Box::new(right),
                span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
            };
        }

        Ok(left)
    }

    fn parse_bitwise_and_expr(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_shift_expr()?;

        while self.check(&Token::Ampersand) {
            let start = left.span();
            self.advance();
            let right = self.parse_shift_expr()?;
            left = Expr::Binary {
                op: BinOp::BitAnd,
                left: Box::new(left),
                right: Box::new(right),
                span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
            };
        }

        Ok(left)
    }

    fn parse_shift_expr(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_additive_expr()?;

        loop {
            let op = match self.peek_token() {
                Token::Shl => BinOp::Shl,
                Token::Shr => BinOp::Shr,
                _ => break,
            };
            let start = left.span();
            self.advance();
            let right = self.parse_additive_expr()?;
            left = Expr::Binary {
                op,
                left: Box::new(left),
                right: Box::new(right),
                span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
            };
        }

        Ok(left)
    }

    fn parse_additive_expr(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_multiplicative_expr()?;

        loop {
            let op = match self.peek_token() {
                Token::Plus => BinOp::Add,
                Token::Minus => BinOp::Sub,
                _ => break,
            };
            let start = left.span();
            self.advance();
            let right = self.parse_multiplicative_expr()?;
            left = Expr::Binary {
                op,
                left: Box::new(left),
                right: Box::new(right),
                span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
            };
        }

        Ok(left)
    }

    fn parse_multiplicative_expr(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_unary_expr()?;

        loop {
            let op = match self.peek_token() {
                Token::Star => BinOp::Mul,
                Token::Slash => BinOp::Div,
                Token::Percent => BinOp::Mod,
                _ => break,
            };
            let start = left.span();
            self.advance();
            let right = self.parse_unary_expr()?;
            left = Expr::Binary {
                op,
                left: Box::new(left),
                right: Box::new(right),
                span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
            };
        }

        Ok(left)
    }

    fn parse_unary_expr(&mut self) -> Result<Expr, ParseError> {
        let start = self.current_span();
        match self.peek_token() {
            Token::Not => {
                self.advance();
                let expr = self.parse_unary_expr()?;
                Ok(Expr::Unary {
                    op: UnOp::Not,
                    expr: Box::new(expr),
                    span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
                })
            }
            Token::Minus => {
                self.advance();
                let expr = self.parse_unary_expr()?;
                Ok(Expr::Unary {
                    op: UnOp::Neg,
                    expr: Box::new(expr),
                    span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
                })
            }
            Token::Tilde => {
                self.advance();
                let expr = self.parse_unary_expr()?;
                Ok(Expr::Unary {
                    op: UnOp::BitNot,
                    expr: Box::new(expr),
                    span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
                })
            }
            _ => self.parse_cast_expr(),
        }
    }

    fn parse_cast_expr(&mut self) -> Result<Expr, ParseError> {
        let mut expr = self.parse_postfix_expr()?;

        while self.check(&Token::As) {
            let start = expr.span();
            self.advance();
            let ty = self.parse_type()?;
            expr = Expr::Cast {
                expr: Box::new(expr),
                ty,
                span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
            };
        }

        Ok(expr)
    }

    fn parse_postfix_expr(&mut self) -> Result<Expr, ParseError> {
        let mut expr = self.parse_primary_expr()?;

        loop {
            match self.peek_token() {
                Token::Dot => {
                    let start = expr.span();
                    self.advance();
                    let field = self.parse_ident()?;

                    // Check for method call
                    if self.check(&Token::LParen) {
                        self.advance();
                        let args = self.parse_call_args()?;
                        self.consume(&Token::RParen, ")")?;
                        expr = Expr::MethodCall {
                            receiver: Box::new(expr),
                            method: field,
                            args,
                            span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
                        };
                    } else {
                        expr = Expr::FieldAccess {
                            expr: Box::new(expr),
                            field,
                            span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
                        };
                    }
                }
                Token::LBracket => {
                    let start = expr.span();
                    self.advance();
                    let index = self.parse_expr()?;
                    self.consume(&Token::RBracket, "]")?;
                    expr = Expr::Index {
                        expr: Box::new(expr),
                        index: Box::new(index),
                        span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
                    };
                }
                _ => break,
            }
        }

        Ok(expr)
    }

    fn parse_primary_expr(&mut self) -> Result<Expr, ParseError> {
        let start = self.current_span();

        match self.peek_token().clone() {
            Token::True => {
                self.advance();
                Ok(Expr::Literal {
                    value: Literal::Bool(true),
                    span: start,
                })
            }
            Token::False => {
                self.advance();
                Ok(Expr::Literal {
                    value: Literal::Bool(false),
                    span: start,
                })
            }
            Token::IntLit(n) => {
                self.advance();
                Ok(Expr::Literal {
                    value: Literal::Int(n),
                    span: start,
                })
            }
            Token::FixedLit(n) => {
                self.advance();
                Ok(Expr::Literal {
                    value: Literal::Fixed(n),
                    span: start,
                })
            }
            Token::StringLit(s) => {
                self.advance();
                Ok(Expr::Literal {
                    value: Literal::String(s),
                    span: start,
                })
            }
            Token::Ident(name) => {
                self.advance();

                // Check for struct init, function call, or simple ident
                if self.check(&Token::LBrace) && self.looks_like_struct_init() {
                    // Struct initialization: requires `Ident { field: value, ... }`
                    // We look ahead to distinguish from `ident { statement... }`
                    self.advance();
                    let mut fields = Vec::new();
                    while !self.check(&Token::RBrace) {
                        let field_name = self.parse_ident()?;
                        self.consume(&Token::Colon, ":")?;
                        let field_value = self.parse_expr()?;
                        fields.push((field_name, field_value));
                        if !self.check(&Token::RBrace) {
                            self.consume(&Token::Comma, ",")?;
                        }
                    }
                    self.consume(&Token::RBrace, "}")?;
                    Ok(Expr::StructInit {
                        name,
                        fields,
                        span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
                    })
                } else if self.check(&Token::LParen) {
                    // Function call
                    self.advance();
                    let args = self.parse_call_args()?;
                    self.consume(&Token::RParen, ")")?;
                    Ok(Expr::Call {
                        func: name,
                        args,
                        span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
                    })
                } else if self.check(&Token::ColonColon) {
                    // Enum variant
                    self.advance();
                    let variant = self.parse_ident()?;
                    Ok(Expr::Ident {
                        name: format!("{}::{}", name, variant),
                        span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
                    })
                } else {
                    Ok(Expr::Ident { name, span: start })
                }
            }
            Token::LParen => {
                self.advance();
                if self.check(&Token::RParen) {
                    self.advance();
                    Ok(Expr::Literal {
                        value: Literal::Unit,
                        span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
                    })
                } else {
                    let expr = self.parse_expr()?;
                    self.consume(&Token::RParen, ")")?;
                    Ok(expr)
                }
            }
            Token::LBracket => {
                self.advance();
                let mut elements = Vec::new();
                while !self.check(&Token::RBracket) {
                    elements.push(self.parse_expr()?);
                    if !self.check(&Token::RBracket) {
                        self.consume(&Token::Comma, ",")?;
                    }
                }
                self.consume(&Token::RBracket, "]")?;
                Ok(Expr::ArrayInit {
                    elements,
                    span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
                })
            }
            Token::If => {
                self.advance();
                let cond = self.parse_expr()?;
                self.consume(&Token::LBrace, "{")?;
                let then_expr = self.parse_expr()?;
                self.consume(&Token::RBrace, "}")?;
                self.consume(&Token::Else, "else")?;
                self.consume(&Token::LBrace, "{")?;
                let else_expr = self.parse_expr()?;
                self.consume(&Token::RBrace, "}")?;
                Ok(Expr::IfExpr {
                    cond: Box::new(cond),
                    then_expr: Box::new(then_expr),
                    else_expr: Box::new(else_expr),
                    span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
                })
            }
            Token::LBrace => {
                let block = self.parse_block()?;
                Ok(Expr::Block {
                    block,
                    span: Span::new(start.start, self.tokens[self.pos - 1].span.end, start.line, start.column),
                })
            }
            _ => Err(ParseError::InvalidExpr(start)),
        }
    }

    fn parse_call_args(&mut self) -> Result<Vec<Expr>, ParseError> {
        let mut args = Vec::new();
        while !self.check(&Token::RParen) {
            args.push(self.parse_expr()?);
            if !self.check(&Token::RParen) {
                self.consume(&Token::Comma, ",")?;
            }
        }
        Ok(args)
    }

    fn parse_ident(&mut self) -> Result<String, ParseError> {
        match self.peek_token().clone() {
            Token::Ident(name) => {
                self.advance();
                Ok(name)
            }
            _ => Err(ParseError::UnexpectedToken {
                expected: "identifier".to_string(),
                found: format!("{:?}", self.peek_token()),
                span: self.current_span(),
            }),
        }
    }

    fn parse_int_literal(&mut self) -> Result<i64, ParseError> {
        match self.peek_token().clone() {
            Token::IntLit(n) => {
                self.advance();
                Ok(n)
            }
            _ => Err(ParseError::UnexpectedToken {
                expected: "integer literal".to_string(),
                found: format!("{:?}", self.peek_token()),
                span: self.current_span(),
            }),
        }
    }
}

/// Parse source code into AST
pub fn parse(source: &str) -> Result<Program, crate::error::EkklError> {
    let tokens = crate::lexer::tokenize(source)?;
    let mut parser = Parser::new(tokens);
    Ok(parser.parse_program()?)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_fn() {
        let program = parse("fn foo(x: i32, y: q16_16) -> i32 { return x }").unwrap();
        assert_eq!(program.items.len(), 1);
        if let Item::FnDef(f) = &program.items[0] {
            assert_eq!(f.name, "foo");
            assert_eq!(f.params.len(), 2);
            assert!(f.return_type.is_some());
        } else {
            panic!("Expected FnDef");
        }
    }

    #[test]
    fn test_parse_struct() {
        let program = parse("struct Point { x: i32, y: i32 }").unwrap();
        assert_eq!(program.items.len(), 1);
        if let Item::StructDef(s) = &program.items[0] {
            assert_eq!(s.name, "Point");
            assert_eq!(s.fields.len(), 2);
        } else {
            panic!("Expected StructDef");
        }
    }

    #[test]
    fn test_parse_expr() {
        let program = parse("fn test() { let x = 1 + 2 * 3 }").unwrap();
        // Should parse as 1 + (2 * 3) due to precedence
        if let Item::FnDef(f) = &program.items[0] {
            if let Stmt::Let { init, .. } = &f.body.stmts[0] {
                if let Expr::Binary { op: BinOp::Add, .. } = init {
                    // Correct!
                } else {
                    panic!("Expected Add at top level");
                }
            }
        }
    }

    #[test]
    fn test_parse_if() {
        let program = parse("fn test() { if x > 0 { return 1 } else { return 0 } }").unwrap();
        if let Item::FnDef(f) = &program.items[0] {
            if let Stmt::If { else_block, .. } = &f.body.stmts[0] {
                assert!(else_block.is_some());
            } else {
                panic!("Expected If");
            }
        }
    }
}
