//! Evaluator tests for EKKL

use ekkl::{parse, Evaluator, Fixed, Value};

#[test]
fn test_eval_arithmetic() {
    let source = r#"
        fn add(a: i32, b: i32) -> i32 { return a + b }
        fn sub(a: i32, b: i32) -> i32 { return a - b }
        fn mul(a: i32, b: i32) -> i32 { return a * b }
        fn div(a: i32, b: i32) -> i32 { return a / b }
    "#;

    let program = parse(source).unwrap();
    let mut eval = Evaluator::new();
    eval.load_program(&program).unwrap();

    assert_eq!(
        eval.call("add", vec![Value::I32(2), Value::I32(3)])
            .unwrap()
            .as_i64()
            .unwrap(),
        5
    );
    assert_eq!(
        eval.call("sub", vec![Value::I32(5), Value::I32(3)])
            .unwrap()
            .as_i64()
            .unwrap(),
        2
    );
    assert_eq!(
        eval.call("mul", vec![Value::I32(4), Value::I32(3)])
            .unwrap()
            .as_i64()
            .unwrap(),
        12
    );
    assert_eq!(
        eval.call("div", vec![Value::I32(10), Value::I32(3)])
            .unwrap()
            .as_i64()
            .unwrap(),
        3
    );
}

#[test]
fn test_eval_comparison() {
    let source = r#"
        fn lt(a: i32, b: i32) -> bool { return a < b }
        fn gt(a: i32, b: i32) -> bool { return a > b }
        fn eq(a: i32, b: i32) -> bool { return a == b }
    "#;

    let program = parse(source).unwrap();
    let mut eval = Evaluator::new();
    eval.load_program(&program).unwrap();

    assert_eq!(
        eval.call("lt", vec![Value::I32(1), Value::I32(2)])
            .unwrap()
            .as_bool()
            .unwrap(),
        true
    );
    assert_eq!(
        eval.call("gt", vec![Value::I32(2), Value::I32(1)])
            .unwrap()
            .as_bool()
            .unwrap(),
        true
    );
    assert_eq!(
        eval.call("eq", vec![Value::I32(5), Value::I32(5)])
            .unwrap()
            .as_bool()
            .unwrap(),
        true
    );
}

#[test]
fn test_eval_logical() {
    let source = r#"
        fn and_op(a: bool, b: bool) -> bool { return a && b }
        fn or_op(a: bool, b: bool) -> bool { return a || b }
        fn not_op(a: bool) -> bool { return !a }
    "#;

    let program = parse(source).unwrap();
    let mut eval = Evaluator::new();
    eval.load_program(&program).unwrap();

    assert_eq!(
        eval.call("and_op", vec![Value::Bool(true), Value::Bool(false)])
            .unwrap()
            .as_bool()
            .unwrap(),
        false
    );
    assert_eq!(
        eval.call("or_op", vec![Value::Bool(true), Value::Bool(false)])
            .unwrap()
            .as_bool()
            .unwrap(),
        true
    );
    assert_eq!(
        eval.call("not_op", vec![Value::Bool(true)])
            .unwrap()
            .as_bool()
            .unwrap(),
        false
    );
}

#[test]
fn test_eval_conditionals() {
    let source = r#"
        fn max(a: i32, b: i32) -> i32 {
            if a > b {
                return a
            } else {
                return b
            }
        }

        fn abs(x: i32) -> i32 {
            if x < 0 {
                return -x
            }
            return x
        }
    "#;

    let program = parse(source).unwrap();
    let mut eval = Evaluator::new();
    eval.load_program(&program).unwrap();

    assert_eq!(
        eval.call("max", vec![Value::I32(5), Value::I32(3)])
            .unwrap()
            .as_i64()
            .unwrap(),
        5
    );
    assert_eq!(
        eval.call("max", vec![Value::I32(2), Value::I32(7)])
            .unwrap()
            .as_i64()
            .unwrap(),
        7
    );
    assert_eq!(
        eval.call("abs", vec![Value::I32(-5)])
            .unwrap()
            .as_i64()
            .unwrap(),
        5
    );
}

#[test]
fn test_eval_for_loop() {
    let source = r#"
        fn sum(n: i32) -> i32 {
            let mut total = 0
            for i in 0..n {
                total = total + i
            }
            return total
        }
    "#;

    let program = parse(source).unwrap();
    let mut eval = Evaluator::new();
    eval.load_program(&program).unwrap();

    // sum of 0..5 = 0+1+2+3+4 = 10
    assert_eq!(
        eval.call("sum", vec![Value::I32(5)])
            .unwrap()
            .as_i64()
            .unwrap(),
        10
    );
}

#[test]
fn test_eval_while_loop() {
    let source = r#"
        fn countdown(n: i32) -> i32 {
            let mut x = n
            let mut count = 0
            while x > 0 {
                x = x - 1
                count = count + 1
            }
            return count
        }
    "#;

    let program = parse(source).unwrap();
    let mut eval = Evaluator::new();
    eval.load_program(&program).unwrap();

    assert_eq!(
        eval.call("countdown", vec![Value::I32(5)])
            .unwrap()
            .as_i64()
            .unwrap(),
        5
    );
}

#[test]
fn test_eval_fixed_point_arithmetic() {
    let source = r#"
        fn add_fixed(a: q16_16, b: q16_16) -> q16_16 {
            return a + b
        }

        fn mul_fixed(a: q16_16, b: q16_16) -> q16_16 {
            return a * b
        }
    "#;

    let program = parse(source).unwrap();
    let mut eval = Evaluator::new();
    eval.load_program(&program).unwrap();

    // 0.5 + 0.25 = 0.75
    let result = eval
        .call(
            "add_fixed",
            vec![
                Value::Fixed(Fixed::from_num(0.5)),
                Value::Fixed(Fixed::from_num(0.25)),
            ],
        )
        .unwrap();
    if let Value::Fixed(f) = result {
        let diff = (f - Fixed::from_num(0.75)).abs();
        assert!(diff < Fixed::from_num(0.001));
    }

    // 0.5 * 0.5 = 0.25
    let result = eval
        .call(
            "mul_fixed",
            vec![
                Value::Fixed(Fixed::from_num(0.5)),
                Value::Fixed(Fixed::from_num(0.5)),
            ],
        )
        .unwrap();
    if let Value::Fixed(f) = result {
        let diff = (f - Fixed::from_num(0.25)).abs();
        assert!(diff < Fixed::from_num(0.001));
    }
}

#[test]
fn test_eval_gradient_function() {
    let source = r#"
        fn gradient(my_load: q16_16, neighbor_load: q16_16) -> q16_16 {
            return neighbor_load - my_load
        }
    "#;

    let program = parse(source).unwrap();
    let mut eval = Evaluator::new();
    eval.load_program(&program).unwrap();

    // gradient(0.3, 0.7) = 0.4
    let result = eval
        .call(
            "gradient",
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
        panic!("Expected Fixed value");
    }
}

#[test]
fn test_eval_builtin_constants() {
    let source = r#"
        fn get_k() -> u8 {
            return K_NEIGHBORS
        }

        fn get_threshold() -> q16_16 {
            return SUPERMAJORITY
        }
    "#;

    let program = parse(source).unwrap();
    let mut eval = Evaluator::new();
    eval.load_program(&program).unwrap();

    assert_eq!(
        eval.call("get_k", vec![]).unwrap().as_u8().unwrap(),
        7
    );

    let threshold = eval.call("get_threshold", vec![]).unwrap();
    if let Value::Fixed(f) = threshold {
        let expected = Fixed::from_bits(0xAB85); // ~0.67
        assert_eq!(f, expected);
    }
}

#[test]
fn test_eval_recursion() {
    let source = r#"
        fn factorial(n: i32) -> i32 {
            if n <= 1 {
                return 1
            }
            return n * factorial(n - 1)
        }
    "#;

    let program = parse(source).unwrap();
    let mut eval = Evaluator::new();
    eval.load_program(&program).unwrap();

    assert_eq!(
        eval.call("factorial", vec![Value::I32(5)])
            .unwrap()
            .as_i64()
            .unwrap(),
        120
    );
}

#[test]
fn test_eval_struct() {
    let source = r#"
        struct Point {
            x: i32,
            y: i32,
        }

        fn make_point(x: i32, y: i32) -> Point {
            return Point { x: x, y: y }
        }

        fn get_x(p: Point) -> i32 {
            return p.x
        }
    "#;

    let program = parse(source).unwrap();
    let mut eval = Evaluator::new();
    eval.load_program(&program).unwrap();

    let point = eval
        .call("make_point", vec![Value::I32(10), Value::I32(20)])
        .unwrap();

    if let Value::Struct { name, fields } = &point {
        assert_eq!(name, "Point");
        assert_eq!(fields.get("x").unwrap().as_i64().unwrap(), 10);
        assert_eq!(fields.get("y").unwrap().as_i64().unwrap(), 20);
    } else {
        panic!("Expected Struct");
    }

    // Test field access
    let x = eval.call("get_x", vec![point]).unwrap();
    assert_eq!(x.as_i64().unwrap(), 10);
}

#[test]
fn test_eval_array() {
    let source = r#"
        fn sum_array() -> i32 {
            let arr = [1, 2, 3, 4, 5]
            let mut total = 0
            for i in 0..5 {
                total = total + arr[i]
            }
            return total
        }
    "#;

    let program = parse(source).unwrap();
    let mut eval = Evaluator::new();
    eval.load_program(&program).unwrap();

    assert_eq!(
        eval.call("sum_array", vec![]).unwrap().as_i64().unwrap(),
        15
    );
}

#[test]
fn test_eval_cast() {
    let source = r#"
        fn to_u8(x: i32) -> u8 {
            return x as u8
        }
    "#;

    let program = parse(source).unwrap();
    let mut eval = Evaluator::new();
    eval.load_program(&program).unwrap();

    let result = eval.call("to_u8", vec![Value::I32(255)]).unwrap();
    assert_eq!(result.as_u8().unwrap(), 255);
}

#[test]
fn test_eval_builtin_functions() {
    let source = r#"
        fn test_abs() -> q16_16 {
            return q16_abs(-0.5q)
        }

        fn test_clamp() -> q16_16 {
            return q16_clamp(1.5q, 0.0q, 1.0q)
        }
    "#;

    let program = parse(source).unwrap();
    let mut eval = Evaluator::new();
    eval.load_program(&program).unwrap();

    let abs_result = eval.call("test_abs", vec![]).unwrap();
    if let Value::Fixed(f) = abs_result {
        let diff = (f - Fixed::from_num(0.5)).abs();
        assert!(diff < Fixed::from_num(0.001));
    }

    let clamp_result = eval.call("test_clamp", vec![]).unwrap();
    if let Value::Fixed(f) = clamp_result {
        assert_eq!(f, Fixed::ONE); // Clamped to 1.0
    }
}

#[test]
fn test_eval_enum() {
    let source = r#"
        fn get_alive() -> HealthState {
            return HealthState::Alive
        }

        fn is_alive(state: HealthState) -> bool {
            if state == HealthState::Alive {
                return true
            }
            return false
        }
    "#;

    let program = parse(source).unwrap();
    let mut eval = Evaluator::new();
    eval.load_program(&program).unwrap();

    let alive = eval.call("get_alive", vec![]).unwrap();
    if let Value::Enum { enum_name, variant } = &alive {
        assert_eq!(enum_name, "HealthState");
        assert_eq!(variant, "Alive");
    } else {
        panic!("Expected Enum");
    }

    let is_alive = eval.call("is_alive", vec![alive]).unwrap();
    assert_eq!(is_alive.as_bool().unwrap(), true);
}

#[test]
fn test_eval_multiple_functions() {
    let source = r#"
        fn helper(x: i32) -> i32 {
            return x * 2
        }

        fn main_func(x: i32) -> i32 {
            let doubled = helper(x)
            return doubled + 1
        }
    "#;

    let program = parse(source).unwrap();
    let mut eval = Evaluator::new();
    eval.load_program(&program).unwrap();

    // main_func(5) = helper(5) + 1 = 10 + 1 = 11
    assert_eq!(
        eval.call("main_func", vec![Value::I32(5)])
            .unwrap()
            .as_i64()
            .unwrap(),
        11
    );
}
