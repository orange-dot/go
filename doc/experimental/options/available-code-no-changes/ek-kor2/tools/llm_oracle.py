#!/usr/bin/env python3
"""
LLM Oracle for Test Vector Generation and Validation

Uses LLM (via API) to:
1. Generate expected outputs from spec + input
2. Generate new test vectors from spec
3. Analyze test failures and suggest fixes

Useful for:
- Hackathon demos (AI-assisted testing)
- Generating edge case test vectors
- Understanding spec ambiguities

Usage:
    python llm_oracle.py expected spec/api.md test_vector.json
    python llm_oracle.py generate spec/api.md field_gradient
    python llm_oracle.py analyze spec/api.md expected.json actual.json

Environment:
    ANTHROPIC_API_KEY or OPENAI_API_KEY must be set.
    Uses Claude by default, falls back to GPT-4.
"""

import json
import sys
import os
import hashlib
import argparse
from pathlib import Path
from typing import Optional, Dict, Any
from datetime import datetime

# Paths
SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent
SPEC_DIR = PROJECT_ROOT / "spec"
TEST_VECTORS_DIR = SPEC_DIR / "test-vectors"
CACHE_DIR = SPEC_DIR / "llm-cache"


def get_cache_key(spec_content: str, input_content: str, operation: str) -> str:
    """Generate cache key from inputs."""
    combined = f"{operation}:{spec_content}:{input_content}"
    return hashlib.sha256(combined.encode()).hexdigest()[:16]


def load_cache(cache_key: str) -> Optional[Dict]:
    """Load cached LLM response."""
    cache_file = CACHE_DIR / f"{cache_key}.json"
    if cache_file.exists():
        with open(cache_file) as f:
            return json.load(f)
    return None


def save_cache(cache_key: str, response: Dict):
    """Save LLM response to cache."""
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    cache_file = CACHE_DIR / f"{cache_key}.json"
    with open(cache_file, 'w') as f:
        json.dump(response, f, indent=2)


def call_llm(prompt: str, system: str = "") -> Optional[str]:
    """
    Call LLM API (Claude or GPT-4).

    Tries Anthropic first, falls back to OpenAI.
    """
    # Try Anthropic (Claude)
    anthropic_key = os.environ.get("ANTHROPIC_API_KEY")
    if anthropic_key:
        try:
            import anthropic
            client = anthropic.Anthropic(api_key=anthropic_key)
            message = client.messages.create(
                model="claude-sonnet-4-20250514",
                max_tokens=4096,
                system=system if system else "You are a helpful assistant for embedded systems testing.",
                messages=[{"role": "user", "content": prompt}]
            )
            return message.content[0].text
        except ImportError:
            print("anthropic package not installed. pip install anthropic", file=sys.stderr)
        except Exception as e:
            print(f"Anthropic API error: {e}", file=sys.stderr)

    # Try OpenAI
    openai_key = os.environ.get("OPENAI_API_KEY")
    if openai_key:
        try:
            import openai
            client = openai.OpenAI(api_key=openai_key)
            response = client.chat.completions.create(
                model="gpt-4",
                messages=[
                    {"role": "system", "content": system or "You are a helpful assistant."},
                    {"role": "user", "content": prompt}
                ],
                max_tokens=4096
            )
            return response.choices[0].message.content
        except ImportError:
            print("openai package not installed. pip install openai", file=sys.stderr)
        except Exception as e:
            print(f"OpenAI API error: {e}", file=sys.stderr)

    return None


def generate_expected(spec_file: Path, vector_file: Path, use_cache: bool = True) -> Optional[Dict]:
    """
    Use LLM to generate expected output for a test vector.

    Args:
        spec_file: Path to API specification (e.g., spec/api.md)
        vector_file: Path to test vector JSON
        use_cache: Whether to use cached responses

    Returns:
        Dict with expected output, or None on failure
    """
    # Load files
    with open(spec_file) as f:
        spec_content = f.read()

    with open(vector_file) as f:
        vector = json.load(f)

    # Check cache
    cache_key = get_cache_key(spec_content, json.dumps(vector), "expected")
    if use_cache:
        cached = load_cache(cache_key)
        if cached:
            print("Using cached response", file=sys.stderr)
            return cached

    # Build prompt
    prompt = f"""Given this API specification:

```markdown
{spec_content[:8000]}  # Truncate for token limits
```

And this test vector input:

```json
{json.dumps(vector, indent=2)}
```

What should the expected output be? Respond with ONLY valid JSON in this format:
{{
    "return": "OK",  // or error string
    // ... other output fields based on the function
}}

Consider:
1. The module: {vector.get('module', 'unknown')}
2. The function: {vector.get('function', 'unknown')}
3. The input parameters
4. Edge cases and error conditions
"""

    system = """You are an expert embedded systems engineer helping validate test vectors for a distributed RTOS.
Output ONLY valid JSON. No explanations, just the JSON response."""

    response = call_llm(prompt, system)
    if response is None:
        return None

    # Parse JSON from response
    try:
        # Try to extract JSON from response
        response = response.strip()
        if response.startswith("```"):
            # Remove markdown code blocks
            lines = response.split("\n")
            response = "\n".join(l for l in lines if not l.startswith("```"))

        result = json.loads(response)

        # Cache result
        cached_result = {
            "_meta": {
                "generated_at": datetime.utcnow().isoformat() + "Z",
                "spec_file": str(spec_file),
                "vector_file": str(vector_file),
            },
            "expected": result
        }
        save_cache(cache_key, cached_result)

        return cached_result

    except json.JSONDecodeError as e:
        print(f"Failed to parse LLM response as JSON: {e}", file=sys.stderr)
        print(f"Response was: {response[:500]}", file=sys.stderr)
        return None


def generate_test_vector(spec_file: Path, function_name: str, use_cache: bool = True) -> Optional[Dict]:
    """
    Use LLM to generate a new test vector from spec.

    Args:
        spec_file: Path to API specification
        function_name: Name of function to test (e.g., "field_gradient")
        use_cache: Whether to use cached responses

    Returns:
        Complete test vector dict, or None on failure
    """
    with open(spec_file) as f:
        spec_content = f.read()

    cache_key = get_cache_key(spec_content, function_name, "generate")
    if use_cache:
        cached = load_cache(cache_key)
        if cached:
            print("Using cached response", file=sys.stderr)
            return cached

    prompt = f"""Given this API specification:

```markdown
{spec_content[:8000]}
```

Generate a complete test vector for the function: {function_name}

The test vector should:
1. Test an interesting edge case or boundary condition
2. Have clear expected output
3. Follow the existing test vector format

Respond with ONLY valid JSON in this format:
{{
    "id": "generated_001",
    "name": "{function_name}_edge_case",
    "module": "...",
    "function": "{function_name}",
    "description": "Tests ...",
    "input": {{ ... }},
    "expected": {{ "return": "OK", ... }},
    "notes": ["Generated by LLM oracle"]
}}
"""

    system = """You are an expert test engineer generating test vectors for an embedded RTOS.
Generate creative edge cases that might reveal bugs.
Output ONLY valid JSON. No explanations."""

    response = call_llm(prompt, system)
    if response is None:
        return None

    try:
        response = response.strip()
        if response.startswith("```"):
            lines = response.split("\n")
            response = "\n".join(l for l in lines if not l.startswith("```"))

        result = json.loads(response)

        cached_result = {
            "_meta": {
                "generated_at": datetime.utcnow().isoformat() + "Z",
                "spec_file": str(spec_file),
                "function": function_name,
            },
            "vector": result
        }
        save_cache(cache_key, cached_result)

        return cached_result

    except json.JSONDecodeError as e:
        print(f"Failed to parse LLM response: {e}", file=sys.stderr)
        return None


def analyze_failure(spec_file: Path, expected_file: Path, actual_file: Path) -> Optional[str]:
    """
    Use LLM to analyze a test failure and suggest root cause.

    Args:
        spec_file: Path to API specification
        expected_file: Path to expected output JSON
        actual_file: Path to actual output JSON

    Returns:
        Analysis string, or None on failure
    """
    with open(spec_file) as f:
        spec_content = f.read()

    with open(expected_file) as f:
        expected = json.load(f)

    with open(actual_file) as f:
        actual = json.load(f)

    prompt = f"""A test is failing. Analyze the discrepancy.

API Specification (excerpt):
```markdown
{spec_content[:4000]}
```

Expected output:
```json
{json.dumps(expected, indent=2)}
```

Actual output:
```json
{json.dumps(actual, indent=2)}
```

Please analyze:
1. What is the specific discrepancy?
2. Is the expected output correct according to the spec?
3. Is the actual output a valid interpretation of the spec?
4. What might cause this difference in the implementation?
5. Recommendation: fix expected, fix implementation, or clarify spec?
"""

    system = """You are a senior embedded systems engineer debugging test failures.
Be precise and technical. Cite specific parts of the spec."""

    return call_llm(prompt, system)


def main():
    parser = argparse.ArgumentParser(description="LLM Oracle for Test Vectors")
    subparsers = parser.add_subparsers(dest="command", help="Command to run")

    # expected command
    expected_parser = subparsers.add_parser("expected", help="Generate expected output")
    expected_parser.add_argument("spec", help="Path to spec file (e.g., spec/api.md)")
    expected_parser.add_argument("vector", help="Path to test vector JSON")
    expected_parser.add_argument("--no-cache", action="store_true", help="Skip cache")

    # generate command
    generate_parser = subparsers.add_parser("generate", help="Generate new test vector")
    generate_parser.add_argument("spec", help="Path to spec file")
    generate_parser.add_argument("function", help="Function name to test")
    generate_parser.add_argument("--no-cache", action="store_true", help="Skip cache")

    # analyze command
    analyze_parser = subparsers.add_parser("analyze", help="Analyze test failure")
    analyze_parser.add_argument("spec", help="Path to spec file")
    analyze_parser.add_argument("expected", help="Path to expected output JSON")
    analyze_parser.add_argument("actual", help="Path to actual output JSON")

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        print("\nEnvironment variables needed:")
        print("  ANTHROPIC_API_KEY - for Claude API")
        print("  OPENAI_API_KEY - for GPT-4 API (fallback)")
        return 1

    if args.command == "expected":
        result = generate_expected(
            Path(args.spec),
            Path(args.vector),
            use_cache=not args.no_cache
        )
        if result:
            print(json.dumps(result, indent=2))
            return 0
        return 1

    elif args.command == "generate":
        result = generate_test_vector(
            Path(args.spec),
            args.function,
            use_cache=not args.no_cache
        )
        if result:
            print(json.dumps(result, indent=2))
            return 0
        return 1

    elif args.command == "analyze":
        result = analyze_failure(
            Path(args.spec),
            Path(args.expected),
            Path(args.actual)
        )
        if result:
            print(result)
            return 0
        return 1

    return 1


if __name__ == "__main__":
    sys.exit(main())
