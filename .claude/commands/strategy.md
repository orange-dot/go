# Strategy Planner

High-level strategic thinking before detailed implementation planning.

## Instructions

1. Understand the goal from `$ARGUMENTS`
2. Research the problem space:
   - What similar solutions exist in the codebase?
   - What patterns are already established?
   - What are the constraints (performance, compatibility, complexity)?
3. Identify 2-3 possible approaches
4. Analyze trade-offs for each approach
5. Consider verification strategy (how will we know it works?)
6. Recommend the best approach with justification

## Output Format

```
## Goal
<what we're trying to achieve>

## Context
<relevant existing code, patterns, constraints>

## Approaches

### Option A: <name>
- Description: <how it works>
- Pros: <benefits>
- Cons: <drawbacks>
- Effort: Low/Medium/High

### Option B: <name>
- Description: <how it works>
- Pros: <benefits>
- Cons: <drawbacks>
- Effort: Low/Medium/High

## Recommendation
<which option and why>

## Verification Strategy
- <how to validate the implementation works>
- <what tests to add>
- <manual checks if needed>

## Open Questions
- <anything that needs clarification before proceeding>
```

## Workflow

After `/strategy`, use:
- `/plan <feature>` for detailed implementation steps
- `/implement <feature>` to execute the plan

## Arguments

- `$ARGUMENTS` - Feature, problem, or goal to strategize
