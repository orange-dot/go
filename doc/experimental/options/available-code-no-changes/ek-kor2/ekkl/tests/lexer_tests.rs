//! Lexer tests for EKKL

use ekkl::lexer::{tokenize, Token};

#[test]
fn test_keywords() {
    let tokens = tokenize("fn let mut if else match for in while return struct enum const true false as")
        .unwrap();

    assert!(matches!(tokens[0].token, Token::Fn));
    assert!(matches!(tokens[1].token, Token::Let));
    assert!(matches!(tokens[2].token, Token::Mut));
    assert!(matches!(tokens[3].token, Token::If));
    assert!(matches!(tokens[4].token, Token::Else));
    assert!(matches!(tokens[5].token, Token::Match));
    assert!(matches!(tokens[6].token, Token::For));
    assert!(matches!(tokens[7].token, Token::In));
    assert!(matches!(tokens[8].token, Token::While));
    assert!(matches!(tokens[9].token, Token::Return));
    assert!(matches!(tokens[10].token, Token::Struct));
    assert!(matches!(tokens[11].token, Token::Enum));
    assert!(matches!(tokens[12].token, Token::Const));
    assert!(matches!(tokens[13].token, Token::True));
    assert!(matches!(tokens[14].token, Token::False));
    assert!(matches!(tokens[15].token, Token::As));
}

#[test]
fn test_type_keywords() {
    let tokens = tokenize("i8 i16 i32 i64 u8 u16 u32 u64 bool q16_16").unwrap();

    assert!(matches!(tokens[0].token, Token::I8));
    assert!(matches!(tokens[1].token, Token::I16));
    assert!(matches!(tokens[2].token, Token::I32));
    assert!(matches!(tokens[3].token, Token::I64));
    assert!(matches!(tokens[4].token, Token::U8));
    assert!(matches!(tokens[5].token, Token::U16));
    assert!(matches!(tokens[6].token, Token::U32));
    assert!(matches!(tokens[7].token, Token::U64));
    assert!(matches!(tokens[8].token, Token::Bool));
    assert!(matches!(tokens[9].token, Token::Q16_16));
}

#[test]
fn test_integer_literals() {
    let tokens = tokenize("0 42 123 0xFF 0x1234").unwrap();

    assert!(matches!(tokens[0].token, Token::IntLit(0)));
    assert!(matches!(tokens[1].token, Token::IntLit(42)));
    assert!(matches!(tokens[2].token, Token::IntLit(123)));
    assert!(matches!(tokens[3].token, Token::IntLit(255)));
    assert!(matches!(tokens[4].token, Token::IntLit(0x1234)));
}

#[test]
fn test_fixed_literals() {
    let tokens = tokenize("0.5q 1.0q 0.0q").unwrap();

    // 0.5 in Q16.16 = 32768
    if let Token::FixedLit(bits) = tokens[0].token {
        assert_eq!(bits, 32768);
    } else {
        panic!("Expected FixedLit");
    }

    // 1.0 in Q16.16 = 65536
    if let Token::FixedLit(bits) = tokens[1].token {
        assert_eq!(bits, 65536);
    } else {
        panic!("Expected FixedLit");
    }

    // 0.0 in Q16.16 = 0
    if let Token::FixedLit(bits) = tokens[2].token {
        assert_eq!(bits, 0);
    } else {
        panic!("Expected FixedLit");
    }
}

#[test]
fn test_operators() {
    let tokens = tokenize("+ - * / % == != < <= > >= && || ! & | ^ ~ << >>").unwrap();

    assert!(matches!(tokens[0].token, Token::Plus));
    assert!(matches!(tokens[1].token, Token::Minus));
    assert!(matches!(tokens[2].token, Token::Star));
    assert!(matches!(tokens[3].token, Token::Slash));
    assert!(matches!(tokens[4].token, Token::Percent));
    assert!(matches!(tokens[5].token, Token::Eq));
    assert!(matches!(tokens[6].token, Token::Ne));
    assert!(matches!(tokens[7].token, Token::Lt));
    assert!(matches!(tokens[8].token, Token::Le));
    assert!(matches!(tokens[9].token, Token::Gt));
    assert!(matches!(tokens[10].token, Token::Ge));
    assert!(matches!(tokens[11].token, Token::And));
    assert!(matches!(tokens[12].token, Token::Or));
    assert!(matches!(tokens[13].token, Token::Not));
    assert!(matches!(tokens[14].token, Token::Ampersand));
    assert!(matches!(tokens[15].token, Token::Pipe));
    assert!(matches!(tokens[16].token, Token::Caret));
    assert!(matches!(tokens[17].token, Token::Tilde));
    assert!(matches!(tokens[18].token, Token::Shl));
    assert!(matches!(tokens[19].token, Token::Shr));
}

#[test]
fn test_delimiters() {
    let tokens = tokenize("= -> => : ; , . .. :: ( ) { } [ ]").unwrap();

    assert!(matches!(tokens[0].token, Token::Assign));
    assert!(matches!(tokens[1].token, Token::Arrow));
    assert!(matches!(tokens[2].token, Token::FatArrow));
    assert!(matches!(tokens[3].token, Token::Colon));
    assert!(matches!(tokens[4].token, Token::Semicolon));
    assert!(matches!(tokens[5].token, Token::Comma));
    assert!(matches!(tokens[6].token, Token::Dot));
    assert!(matches!(tokens[7].token, Token::DotDot));
    assert!(matches!(tokens[8].token, Token::ColonColon));
    assert!(matches!(tokens[9].token, Token::LParen));
    assert!(matches!(tokens[10].token, Token::RParen));
    assert!(matches!(tokens[11].token, Token::LBrace));
    assert!(matches!(tokens[12].token, Token::RBrace));
    assert!(matches!(tokens[13].token, Token::LBracket));
    assert!(matches!(tokens[14].token, Token::RBracket));
}

#[test]
fn test_identifiers() {
    let tokens = tokenize("foo bar_baz FooBar _private").unwrap();

    assert!(matches!(tokens[0].token, Token::Ident(ref s) if s == "foo"));
    assert!(matches!(tokens[1].token, Token::Ident(ref s) if s == "bar_baz"));
    assert!(matches!(tokens[2].token, Token::Ident(ref s) if s == "FooBar"));
    assert!(matches!(tokens[3].token, Token::Ident(ref s) if s == "_private"));
}

#[test]
fn test_string_literals() {
    let tokens = tokenize(r#""hello" "world" "with\nescape""#).unwrap();

    assert!(matches!(tokens[0].token, Token::StringLit(ref s) if s == "hello"));
    assert!(matches!(tokens[1].token, Token::StringLit(ref s) if s == "world"));
    assert!(matches!(tokens[2].token, Token::StringLit(ref s) if s == "with\nescape"));
}

#[test]
fn test_comments() {
    let tokens = tokenize(
        r#"
        fn // this is a comment
        foo /* block comment */ bar
        "#,
    )
    .unwrap();

    assert!(matches!(tokens[0].token, Token::Fn));
    assert!(matches!(tokens[1].token, Token::Ident(ref s) if s == "foo"));
    assert!(matches!(tokens[2].token, Token::Ident(ref s) if s == "bar"));
}

#[test]
fn test_complete_function() {
    let source = r#"
        fn gradient(my_load: q16_16, neighbor_load: q16_16) -> q16_16 {
            return neighbor_load - my_load
        }
    "#;

    let tokens = tokenize(source).unwrap();

    // Just verify it tokenizes without error and has expected structure
    assert!(matches!(tokens[0].token, Token::Fn));
    assert!(matches!(tokens[1].token, Token::Ident(ref s) if s == "gradient"));
    assert!(matches!(tokens.last().unwrap().token, Token::Eof));
}
