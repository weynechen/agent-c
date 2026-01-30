---
name: code-review
description: Perform thorough code reviews following best practices, identify bugs, security issues, and suggest improvements.
license: MIT
compatibility: Works with any programming language
allowed-tools: read_file
---

# Code Review Skill

## When to use this skill

Use this skill when the user asks you to:
- Review code for bugs or issues
- Check code quality
- Find security vulnerabilities
- Suggest improvements to existing code
- Evaluate pull requests or code changes

## Review Process

1. **Understand Context**: First understand what the code is supposed to do
2. **Check for Bugs**: Look for logical errors, edge cases, null pointer issues
3. **Security Review**: Check for common vulnerabilities (injection, XSS, etc.)
4. **Code Quality**: Evaluate readability, maintainability, naming conventions
5. **Performance**: Identify potential performance issues
6. **Best Practices**: Ensure code follows language-specific best practices

## Output Format

Structure your review as follows:

### Summary
Brief overview of the code and its purpose.

### Issues Found
List issues by severity:
- **Critical**: Must fix before merge
- **Major**: Should fix, significant impact
- **Minor**: Nice to fix, low impact
- **Suggestion**: Optional improvements

### Positive Aspects
Highlight what was done well.

### Recommendations
Specific actionable recommendations for improvement.
