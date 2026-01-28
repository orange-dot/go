//! EKKL Lexer
//!
//! Tokenizer using nom parser combinators.

use crate::error::{LexError, Span};
use nom::{
    branch::alt,
    bytes::complete::{tag, take_until, take_while, take_while1},
    character::complete::{char, multispace0, multispace1, one_of},
    combinator::{map, opt, recognize, value},
    multi::many0,
    sequence::{delimited, pair, preceded, terminated, tuple},
    IResult,
};

/// Token with span information
#[derive(Debug, Clone, PartialEq)]
pub struct SpannedToken {
    pub token: Token,
    pub span: Span,
}

/// Token type
#[derive(Debug, Clone, PartialEq)]
pub enum Token {
    // Keywords
    Fn,
    Let,
    Mut,
    If,
    Else,
    Match,
    For,
    In,
    While,
    Return,
    Struct,
    Enum,
    Const,
    True,
    False,
    As,

    // Type keywords
    I8,
    I16,
    I32,
    I64,
    U8,
    U16,
    U32,
    U64,
    Bool,
    Q16_16,

    // Literals
    IntLit(i64),
    FixedLit(i32),   // Raw Q16.16 bits
    StringLit(String),

    // Identifier
    Ident(String),

    // Operators
    Plus,       // +
    Minus,      // -
    Star,       // *
    Slash,      // /
    Percent,    // %
    Ampersand,  // &
    Pipe,       // |
    Caret,      // ^
    Tilde,      // ~
    Shl,        // <<
    Shr,        // >>

    // Comparison
    Eq,         // ==
    Ne,         // !=
    Lt,         // <
    Le,         // <=
    Gt,         // >
    Ge,         // >=

    // Logical
    And,        // &&
    Or,         // ||
    Not,        // !

    // Assignment & Arrows
    Assign,     // =
    Arrow,      // ->
    FatArrow,   // =>

    // Delimiters
    Colon,      // :
    Semicolon,  // ;
    Comma,      // ,
    Dot,        // .
    DotDot,     // ..
    ColonColon, // ::

    // Brackets
    LParen,     // (
    RParen,     // )
    LBrace,     // {
    RBrace,     // }
    LBracket,   // [
    RBracket,   // ]

    // Special
    Underscore, // _
    Eof,
}

/// Lexer state
pub struct Lexer<'a> {
    input: &'a str,
    pos: usize,
    line: usize,
    column: usize,
}

impl<'a> Lexer<'a> {
    pub fn new(input: &'a str) -> Self {
        Self {
            input,
            pos: 0,
            line: 1,
            column: 1,
        }
    }

    /// Tokenize entire input
    pub fn tokenize(&mut self) -> Result<Vec<SpannedToken>, LexError> {
        let mut tokens = Vec::new();

        loop {
            self.skip_whitespace_and_comments();

            if self.is_at_end() {
                tokens.push(SpannedToken {
                    token: Token::Eof,
                    span: self.current_span(),
                });
                break;
            }

            let token = self.next_token()?;
            tokens.push(token);
        }

        Ok(tokens)
    }

    fn is_at_end(&self) -> bool {
        self.pos >= self.input.len()
    }

    fn remaining(&self) -> &str {
        &self.input[self.pos..]
    }

    fn current_span(&self) -> Span {
        Span::new(self.pos, self.pos, self.line, self.column)
    }

    fn advance(&mut self, n: usize) {
        for c in self.input[self.pos..self.pos + n].chars() {
            if c == '\n' {
                self.line += 1;
                self.column = 1;
            } else {
                self.column += 1;
            }
        }
        self.pos += n;
    }

    fn skip_whitespace_and_comments(&mut self) {
        loop {
            let start = self.pos;

            // Skip whitespace
            while !self.is_at_end() {
                let c = self.remaining().chars().next().unwrap();
                if c.is_whitespace() {
                    self.advance(c.len_utf8());
                } else {
                    break;
                }
            }

            // Skip line comments
            if self.remaining().starts_with("//") {
                while !self.is_at_end() {
                    let c = self.remaining().chars().next().unwrap();
                    self.advance(c.len_utf8());
                    if c == '\n' {
                        break;
                    }
                }
            }

            // Skip block comments
            if self.remaining().starts_with("/*") {
                self.advance(2);
                while !self.is_at_end() && !self.remaining().starts_with("*/") {
                    let c = self.remaining().chars().next().unwrap();
                    self.advance(c.len_utf8());
                }
                if self.remaining().starts_with("*/") {
                    self.advance(2);
                }
            }

            if self.pos == start {
                break;
            }
        }
    }

    fn next_token(&mut self) -> Result<SpannedToken, LexError> {
        let start_pos = self.pos;
        let start_line = self.line;
        let start_column = self.column;

        let remaining = self.remaining();

        // Try multi-character operators first
        let (token, len) = if remaining.starts_with("==") {
            (Token::Eq, 2)
        } else if remaining.starts_with("!=") {
            (Token::Ne, 2)
        } else if remaining.starts_with("<=") {
            (Token::Le, 2)
        } else if remaining.starts_with(">=") {
            (Token::Ge, 2)
        } else if remaining.starts_with("&&") {
            (Token::And, 2)
        } else if remaining.starts_with("||") {
            (Token::Or, 2)
        } else if remaining.starts_with("<<") {
            (Token::Shl, 2)
        } else if remaining.starts_with(">>") {
            (Token::Shr, 2)
        } else if remaining.starts_with("->") {
            (Token::Arrow, 2)
        } else if remaining.starts_with("=>") {
            (Token::FatArrow, 2)
        } else if remaining.starts_with("..") {
            (Token::DotDot, 2)
        } else if remaining.starts_with("::") {
            (Token::ColonColon, 2)
        } else {
            // Single character tokens or identifiers/literals
            let c = remaining.chars().next().unwrap();
            match c {
                '+' => (Token::Plus, 1),
                '-' => (Token::Minus, 1),
                '*' => (Token::Star, 1),
                '/' => (Token::Slash, 1),
                '%' => (Token::Percent, 1),
                '&' => (Token::Ampersand, 1),
                '|' => (Token::Pipe, 1),
                '^' => (Token::Caret, 1),
                '~' => (Token::Tilde, 1),
                '<' => (Token::Lt, 1),
                '>' => (Token::Gt, 1),
                '!' => (Token::Not, 1),
                '=' => (Token::Assign, 1),
                ':' => (Token::Colon, 1),
                ';' => (Token::Semicolon, 1),
                ',' => (Token::Comma, 1),
                '.' => (Token::Dot, 1),
                '(' => (Token::LParen, 1),
                ')' => (Token::RParen, 1),
                '{' => (Token::LBrace, 1),
                '}' => (Token::RBrace, 1),
                '[' => (Token::LBracket, 1),
                ']' => (Token::RBracket, 1),
                '_' if !remaining[1..].starts_with(|c: char| c.is_alphanumeric() || c == '_') => {
                    (Token::Underscore, 1)
                }
                '"' => return self.lex_string(),
                c if c.is_ascii_digit() => return self.lex_number(),
                c if c.is_alphabetic() || c == '_' => return self.lex_ident_or_keyword(),
                _ => {
                    return Err(LexError::UnexpectedChar(
                        c,
                        Span::new(start_pos, start_pos + 1, start_line, start_column),
                    ))
                }
            }
        };

        self.advance(len);

        Ok(SpannedToken {
            token,
            span: Span::new(start_pos, self.pos, start_line, start_column),
        })
    }

    fn lex_string(&mut self) -> Result<SpannedToken, LexError> {
        let start_pos = self.pos;
        let start_line = self.line;
        let start_column = self.column;

        self.advance(1); // Skip opening quote

        let mut value = String::new();
        let mut escape = false;

        while !self.is_at_end() {
            let c = self.remaining().chars().next().unwrap();

            if escape {
                match c {
                    'n' => value.push('\n'),
                    'r' => value.push('\r'),
                    't' => value.push('\t'),
                    '\\' => value.push('\\'),
                    '"' => value.push('"'),
                    _ => value.push(c),
                }
                escape = false;
                self.advance(c.len_utf8());
            } else if c == '\\' {
                escape = true;
                self.advance(1);
            } else if c == '"' {
                self.advance(1);
                return Ok(SpannedToken {
                    token: Token::StringLit(value),
                    span: Span::new(start_pos, self.pos, start_line, start_column),
                });
            } else if c == '\n' {
                return Err(LexError::UnterminatedString(Span::new(
                    start_pos,
                    self.pos,
                    start_line,
                    start_column,
                )));
            } else {
                value.push(c);
                self.advance(c.len_utf8());
            }
        }

        Err(LexError::UnterminatedString(Span::new(
            start_pos,
            self.pos,
            start_line,
            start_column,
        )))
    }

    fn lex_number(&mut self) -> Result<SpannedToken, LexError> {
        let start_pos = self.pos;
        let start_line = self.line;
        let start_column = self.column;

        let mut num_str = String::new();
        let mut is_hex = false;
        let mut is_negative = false;

        // Check for negative sign
        if self.remaining().starts_with('-') {
            is_negative = true;
            num_str.push('-');
            self.advance(1);
        }

        // Check for hex prefix
        if self.remaining().starts_with("0x") || self.remaining().starts_with("0X") {
            is_hex = true;
            self.advance(2);
        }

        // Read digits
        while !self.is_at_end() {
            let c = self.remaining().chars().next().unwrap();
            if is_hex && c.is_ascii_hexdigit() {
                num_str.push(c);
                self.advance(1);
            } else if !is_hex && c.is_ascii_digit() {
                num_str.push(c);
                self.advance(1);
            } else if c == '_' {
                // Allow underscores in numbers
                self.advance(1);
            } else {
                break;
            }
        }

        // Check for decimal point (not for hex)
        let has_decimal = if !is_hex && self.remaining().starts_with('.') {
            // Look ahead to see if it's .. (range) or a method call
            let next = self.remaining().chars().nth(1);
            if next.map(|c| c.is_ascii_digit()).unwrap_or(false) {
                num_str.push('.');
                self.advance(1);
                // Read decimal digits
                while !self.is_at_end() {
                    let c = self.remaining().chars().next().unwrap();
                    if c.is_ascii_digit() {
                        num_str.push(c);
                        self.advance(1);
                    } else if c == '_' {
                        self.advance(1);
                    } else {
                        break;
                    }
                }
                true
            } else {
                false
            }
        } else {
            false
        };

        // Check for 'q' suffix for fixed-point
        let is_fixed = if self.remaining().starts_with('q') {
            self.advance(1);
            true
        } else {
            false
        };

        let span = Span::new(start_pos, self.pos, start_line, start_column);

        if is_fixed || has_decimal {
            // Parse as fixed-point Q16.16
            let float_val: f64 = num_str.parse().map_err(|_| LexError::InvalidFixed(span))?;
            let fixed_bits = (float_val * 65536.0) as i32;
            Ok(SpannedToken {
                token: Token::FixedLit(fixed_bits),
                span,
            })
        } else if is_hex {
            // Parse as hex integer
            let val = i64::from_str_radix(&num_str, 16).map_err(|_| LexError::InvalidNumber(span))?;
            Ok(SpannedToken {
                token: Token::IntLit(if is_negative { -val } else { val }),
                span,
            })
        } else {
            // Parse as decimal integer
            let val: i64 = num_str.parse().map_err(|_| LexError::InvalidNumber(span))?;
            Ok(SpannedToken {
                token: Token::IntLit(val),
                span,
            })
        }
    }

    fn lex_ident_or_keyword(&mut self) -> Result<SpannedToken, LexError> {
        let start_pos = self.pos;
        let start_line = self.line;
        let start_column = self.column;

        let mut ident = String::new();

        while !self.is_at_end() {
            let c = self.remaining().chars().next().unwrap();
            if c.is_alphanumeric() || c == '_' {
                ident.push(c);
                self.advance(c.len_utf8());
            } else {
                break;
            }
        }

        let span = Span::new(start_pos, self.pos, start_line, start_column);

        let token = match ident.as_str() {
            // Keywords
            "fn" => Token::Fn,
            "let" => Token::Let,
            "mut" => Token::Mut,
            "if" => Token::If,
            "else" => Token::Else,
            "match" => Token::Match,
            "for" => Token::For,
            "in" => Token::In,
            "while" => Token::While,
            "return" => Token::Return,
            "struct" => Token::Struct,
            "enum" => Token::Enum,
            "const" => Token::Const,
            "true" => Token::True,
            "false" => Token::False,
            "as" => Token::As,
            // Type keywords
            "i8" => Token::I8,
            "i16" => Token::I16,
            "i32" => Token::I32,
            "i64" => Token::I64,
            "u8" => Token::U8,
            "u16" => Token::U16,
            "u32" => Token::U32,
            "u64" => Token::U64,
            "bool" => Token::Bool,
            "q16_16" => Token::Q16_16,
            // Identifier
            _ => Token::Ident(ident),
        };

        Ok(SpannedToken { token, span })
    }
}

/// Convenience function to tokenize a string
pub fn tokenize(input: &str) -> Result<Vec<SpannedToken>, LexError> {
    let mut lexer = Lexer::new(input);
    lexer.tokenize()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_basic_tokens() {
        let tokens = tokenize("fn foo() { }").unwrap();
        assert!(matches!(tokens[0].token, Token::Fn));
        assert!(matches!(tokens[1].token, Token::Ident(ref s) if s == "foo"));
        assert!(matches!(tokens[2].token, Token::LParen));
        assert!(matches!(tokens[3].token, Token::RParen));
        assert!(matches!(tokens[4].token, Token::LBrace));
        assert!(matches!(tokens[5].token, Token::RBrace));
        assert!(matches!(tokens[6].token, Token::Eof));
    }

    #[test]
    fn test_numbers() {
        let tokens = tokenize("42 0xFF 3.14q 0.5q").unwrap();
        assert!(matches!(tokens[0].token, Token::IntLit(42)));
        assert!(matches!(tokens[1].token, Token::IntLit(255)));
        assert!(matches!(tokens[2].token, Token::FixedLit(_)));
        assert!(matches!(tokens[3].token, Token::FixedLit(32768))); // 0.5 * 65536
    }

    #[test]
    fn test_operators() {
        let tokens = tokenize("+ - * / == != && ||").unwrap();
        assert!(matches!(tokens[0].token, Token::Plus));
        assert!(matches!(tokens[1].token, Token::Minus));
        assert!(matches!(tokens[2].token, Token::Star));
        assert!(matches!(tokens[3].token, Token::Slash));
        assert!(matches!(tokens[4].token, Token::Eq));
        assert!(matches!(tokens[5].token, Token::Ne));
        assert!(matches!(tokens[6].token, Token::And));
        assert!(matches!(tokens[7].token, Token::Or));
    }

    #[test]
    fn test_comments() {
        let tokens = tokenize("fn // comment\nfoo /* block */ bar").unwrap();
        assert!(matches!(tokens[0].token, Token::Fn));
        assert!(matches!(tokens[1].token, Token::Ident(ref s) if s == "foo"));
        assert!(matches!(tokens[2].token, Token::Ident(ref s) if s == "bar"));
    }
}
