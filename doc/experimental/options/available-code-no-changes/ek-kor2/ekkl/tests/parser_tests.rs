//! Parser tests for EKKL

use ekkl::ast::{BinOp, Expr, Item, Literal, Stmt, Type};
use ekkl::parser::parse;

#[test]
fn test_parse_empty_function() {
    let program = parse("fn empty() { }").unwrap();
    assert_eq!(program.items.len(), 1);
    if let Item::FnDef(f) = &program.items[0] {
        assert_eq!(f.name, "empty");
        assert!(f.params.is_empty());
        assert!(f.return_type.is_none());
        assert!(f.body.stmts.is_empty());
    } else {
        panic!("Expected FnDef");
    }
}

#[test]
fn test_parse_function_with_params() {
    let program = parse("fn add(a: i32, b: i32) -> i32 { return a + b }").unwrap();

    if let Item::FnDef(f) = &program.items[0] {
        assert_eq!(f.name, "add");
        assert_eq!(f.params.len(), 2);
        assert_eq!(f.params[0].name, "a");
        assert_eq!(f.params[0].ty, Type::I32);
        assert_eq!(f.params[1].name, "b");
        assert_eq!(f.params[1].ty, Type::I32);
        assert_eq!(f.return_type, Some(Type::I32));
    } else {
        panic!("Expected FnDef");
    }
}

#[test]
fn test_parse_struct() {
    let program = parse("struct Point { x: i32, y: i32, z: i32 }").unwrap();

    if let Item::StructDef(s) = &program.items[0] {
        assert_eq!(s.name, "Point");
        assert_eq!(s.fields.len(), 3);
        assert_eq!(s.fields[0].name, "x");
        assert_eq!(s.fields[0].ty, Type::I32);
    } else {
        panic!("Expected StructDef");
    }
}

#[test]
fn test_parse_enum() {
    let program = parse("enum Color { Red, Green, Blue }").unwrap();

    if let Item::EnumDef(e) = &program.items[0] {
        assert_eq!(e.name, "Color");
        assert_eq!(e.variants.len(), 3);
        assert_eq!(e.variants[0], "Red");
        assert_eq!(e.variants[1], "Green");
        assert_eq!(e.variants[2], "Blue");
    } else {
        panic!("Expected EnumDef");
    }
}

#[test]
fn test_parse_const() {
    let program = parse("const MAX_SIZE: u32 = 256").unwrap();

    if let Item::ConstDef(c) = &program.items[0] {
        assert_eq!(c.name, "MAX_SIZE");
        assert_eq!(c.ty, Type::U32);
        if let Expr::Literal { value: Literal::Int(n), .. } = &c.value {
            assert_eq!(*n, 256);
        } else {
            panic!("Expected integer literal");
        }
    } else {
        panic!("Expected ConstDef");
    }
}

#[test]
fn test_parse_let_stmt() {
    let program = parse("fn test() { let x: i32 = 42 }").unwrap();

    if let Item::FnDef(f) = &program.items[0] {
        if let Stmt::Let { name, ty, init, mutable, .. } = &f.body.stmts[0] {
            assert_eq!(name, "x");
            assert_eq!(ty, &Some(Type::I32));
            assert!(!mutable);
            if let Expr::Literal { value: Literal::Int(n), .. } = init {
                assert_eq!(*n, 42);
            } else {
                panic!("Expected integer literal");
            }
        } else {
            panic!("Expected Let statement");
        }
    }
}

#[test]
fn test_parse_mutable_let() {
    let program = parse("fn test() { let mut x = 0 }").unwrap();

    if let Item::FnDef(f) = &program.items[0] {
        if let Stmt::Let { mutable, .. } = &f.body.stmts[0] {
            assert!(mutable);
        }
    }
}

#[test]
fn test_parse_if_stmt() {
    let program = parse("fn test() { if x > 0 { return 1 } }").unwrap();

    if let Item::FnDef(f) = &program.items[0] {
        if let Stmt::If { cond, then_block, else_block, .. } = &f.body.stmts[0] {
            // Check condition is binary > expression
            if let Expr::Binary { op, .. } = cond {
                assert_eq!(*op, BinOp::Gt);
            }
            assert!(!then_block.stmts.is_empty());
            assert!(else_block.is_none());
        } else {
            panic!("Expected If statement");
        }
    }
}

#[test]
fn test_parse_if_else() {
    let program =
        parse("fn test() { if x > 0 { return 1 } else { return 0 } }").unwrap();

    if let Item::FnDef(f) = &program.items[0] {
        if let Stmt::If { else_block, .. } = &f.body.stmts[0] {
            assert!(else_block.is_some());
        }
    }
}

#[test]
fn test_parse_for_loop() {
    let program = parse("fn test() { for i in 0..10 { print(i) } }").unwrap();

    if let Item::FnDef(f) = &program.items[0] {
        if let Stmt::For { var, .. } = &f.body.stmts[0] {
            assert_eq!(var, "i");
        } else {
            panic!("Expected For statement");
        }
    }
}

#[test]
fn test_parse_while_loop() {
    let program = parse("fn test() { while x > 0 { x = x - 1 } }").unwrap();

    if let Item::FnDef(f) = &program.items[0] {
        if let Stmt::While { cond, .. } = &f.body.stmts[0] {
            if let Expr::Binary { op, .. } = cond {
                assert_eq!(*op, BinOp::Gt);
            }
        } else {
            panic!("Expected While statement");
        }
    }
}

#[test]
fn test_parse_match() {
    let program = parse(
        r#"
        fn test() {
            match state {
                Active => 1,
                Inactive => 0,
            }
        }
        "#,
    )
    .unwrap();

    if let Item::FnDef(f) = &program.items[0] {
        if let Stmt::Match { arms, .. } = &f.body.stmts[0] {
            assert_eq!(arms.len(), 2);
        } else {
            panic!("Expected Match statement");
        }
    }
}

#[test]
fn test_parse_operator_precedence() {
    // 1 + 2 * 3 should parse as 1 + (2 * 3)
    let program = parse("fn test() { return 1 + 2 * 3 }").unwrap();

    if let Item::FnDef(f) = &program.items[0] {
        if let Stmt::Return { value: Some(expr), .. } = &f.body.stmts[0] {
            if let Expr::Binary { op: BinOp::Add, right, .. } = expr {
                if let Expr::Binary { op: BinOp::Mul, .. } = right.as_ref() {
                    // Correct precedence!
                } else {
                    panic!("Expected Mul on right side");
                }
            } else {
                panic!("Expected Add at top level");
            }
        }
    }
}

#[test]
fn test_parse_unary() {
    let program = parse("fn test() { return -x }").unwrap();

    if let Item::FnDef(f) = &program.items[0] {
        if let Stmt::Return { value: Some(expr), .. } = &f.body.stmts[0] {
            if let Expr::Unary { .. } = expr {
                // OK
            } else {
                panic!("Expected Unary expression");
            }
        }
    }
}

#[test]
fn test_parse_field_access() {
    let program = parse("fn test() { return point.x }").unwrap();

    if let Item::FnDef(f) = &program.items[0] {
        if let Stmt::Return { value: Some(expr), .. } = &f.body.stmts[0] {
            if let Expr::FieldAccess { field, .. } = expr {
                assert_eq!(field, "x");
            } else {
                panic!("Expected FieldAccess");
            }
        }
    }
}

#[test]
fn test_parse_array_index() {
    let program = parse("fn test() { return arr[0] }").unwrap();

    if let Item::FnDef(f) = &program.items[0] {
        if let Stmt::Return { value: Some(expr), .. } = &f.body.stmts[0] {
            if let Expr::Index { .. } = expr {
                // OK
            } else {
                panic!("Expected Index expression");
            }
        }
    }
}

#[test]
fn test_parse_function_call() {
    let program = parse("fn test() { return add(1, 2) }").unwrap();

    if let Item::FnDef(f) = &program.items[0] {
        if let Stmt::Return { value: Some(expr), .. } = &f.body.stmts[0] {
            if let Expr::Call { func, args, .. } = expr {
                assert_eq!(func, "add");
                assert_eq!(args.len(), 2);
            } else {
                panic!("Expected Call expression");
            }
        }
    }
}

#[test]
fn test_parse_struct_init() {
    let program = parse("fn test() { return Point { x: 1, y: 2 } }").unwrap();

    if let Item::FnDef(f) = &program.items[0] {
        if let Stmt::Return { value: Some(expr), .. } = &f.body.stmts[0] {
            if let Expr::StructInit { name, fields, .. } = expr {
                assert_eq!(name, "Point");
                assert_eq!(fields.len(), 2);
            } else {
                panic!("Expected StructInit");
            }
        }
    }
}

#[test]
fn test_parse_array_init() {
    let program = parse("fn test() { return [1, 2, 3] }").unwrap();

    if let Item::FnDef(f) = &program.items[0] {
        if let Stmt::Return { value: Some(expr), .. } = &f.body.stmts[0] {
            if let Expr::ArrayInit { elements, .. } = expr {
                assert_eq!(elements.len(), 3);
            } else {
                panic!("Expected ArrayInit");
            }
        }
    }
}

#[test]
fn test_parse_cast() {
    let program = parse("fn test() { return x as u8 }").unwrap();

    if let Item::FnDef(f) = &program.items[0] {
        if let Stmt::Return { value: Some(expr), .. } = &f.body.stmts[0] {
            if let Expr::Cast { ty, .. } = expr {
                assert_eq!(ty, &Type::U8);
            } else {
                panic!("Expected Cast expression");
            }
        }
    }
}

#[test]
fn test_parse_array_type() {
    let program = parse("fn test(arr: [i32; 5]) { }").unwrap();

    if let Item::FnDef(f) = &program.items[0] {
        assert_eq!(
            f.params[0].ty,
            Type::Array {
                elem: Box::new(Type::I32),
                size: 5
            }
        );
    }
}

#[test]
fn test_parse_fixed_point() {
    let program = parse("fn test() { let x: q16_16 = 0.5q }").unwrap();

    if let Item::FnDef(f) = &program.items[0] {
        if let Stmt::Let { ty, init, .. } = &f.body.stmts[0] {
            assert_eq!(ty, &Some(Type::Q16_16));
            if let Expr::Literal { value: Literal::Fixed(bits), .. } = init {
                assert_eq!(*bits, 32768); // 0.5 in Q16.16
            }
        }
    }
}
