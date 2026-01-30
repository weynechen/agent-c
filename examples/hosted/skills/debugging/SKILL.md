---
name: debugging
description: Systematic debugging and troubleshooting of code issues, analyzing error messages, and finding root causes.
license: MIT
compatibility: Works with any programming language
---

# Debugging Skill

## When to use this skill

Use this skill when the user:
- Reports a bug or error
- Shares an error message or stack trace
- Needs help understanding why code isn't working
- Wants to fix a runtime issue

## Debugging Methodology

### 1. Gather Information
- What is the expected behavior?
- What is the actual behavior?
- When did the issue start?
- Can it be reproduced consistently?

### 2. Analyze the Error
- Parse error messages and stack traces
- Identify the error type and location
- Understand the execution flow that led to the error

### 3. Form Hypotheses
- List possible causes based on the error
- Prioritize by likelihood
- Consider recent changes

### 4. Investigate
- Examine relevant code sections
- Check variable values and state
- Review related components

### 5. Propose Solutions
- Suggest fixes for the root cause
- Explain why the fix works
- Consider side effects

## Common Error Patterns

### Null/Undefined Errors
- Check for uninitialized variables
- Verify function return values
- Add null checks where needed

### Type Errors
- Verify data types match expectations
- Check type conversions
- Review function signatures

### Logic Errors
- Trace execution flow step by step
- Check conditional statements
- Verify loop boundaries

### Concurrency Issues
- Look for race conditions
- Check lock ordering
- Review shared state access
