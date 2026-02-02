---
name: conventional-commits
description: Write and validate Conventional Commits formatted commit messages. Use when needs to (1) Write commit messages following Conventional Commits specification, (2) Convert existing commit messages to Conventional Commits format, (3) Validate if a commit message follows the specification, (4) Explain or teach Conventional Commits patterns, or (5) Set up tooling for Conventional Commits in a project.
---

# Conventional Commits

A specification for adding human and machine readable meaning to commit messages.

## Quick Reference

```
<type>[optional scope]: <description>

[optional body]

[optional footer(s)]
```

### Types (Required)

| Type | Description | Example |
|------|-------------|---------|
| `feat` | New feature | `feat: add user authentication` |
| `fix` | Bug fix | `fix: resolve null pointer exception` |
| `docs` | Documentation only | `docs: update API reference` |
| `style` | Code style (formatting) | `style: fix indentation` |
| `refactor` | Code restructuring | `refactor: simplify login logic` |
| `perf` | Performance improvement | `perf: optimize database queries` |
| `test` | Adding/correcting tests | `test: add unit tests for auth` |
| `build` | Build system changes | `build: update webpack config` |
| `ci` | CI/CD changes | `ci: add GitHub Actions workflow` |
| `chore` | Maintenance tasks | `chore: update dependencies` |
| `revert` | Revert previous commit | `revert: feat: user auth` |

### Scopes (Optional)

- Use to specify which component is affected
- Can be any noun describing a section of the codebase
- Examples: `feat(api):`, `fix(auth):`, `docs(readme):`

### Breaking Changes

Append `!` after type/scope, or use `BREAKING CHANGE:` in footer:

```
feat(api)!: remove deprecated endpoints

BREAKING CHANGE: The /v1/users endpoint has been removed.
Use /v2/users instead.
```

## Writing Guidelines

1. **Use imperative mood**: "add feature" not "added feature" or "adds feature"
2. **Don't capitalize first letter**: `feat: add feature` not `feat: Add feature`
3. **No trailing period** in description
4. **Keep subject under 50 characters** when possible
5. **Use body to explain what and why**, not how

## Common Patterns

### Simple commit
```
fix: correct typo in README
```

### With scope
```
feat(auth): implement JWT token refresh
```

### With body
```
feat: add user profile page

Add a new page for users to view and edit their profile.
Includes avatar upload and bio editing.
```

### With footer (referencing issues)
```
fix: resolve memory leak in data parser

The parser was not releasing buffer memory after processing
large files. Added explicit cleanup in finally block.

Fixes #123
Closes #456
```

### Breaking change
```
feat(api)!: change response format for /users

BREAKING CHANGE: Response now returns `users` array instead
of direct array at root level.

Migration:
- Old: `data[0].name`
- New: `data.users[0].name`
```

## Validation

A valid Conventional Commit message:
- Has a valid type prefix
- Has a colon and space after type/scope
- Has a non-empty description
- (Optional) Body and footer follow blank line separation

## References

- Full specification: [conventionalcommits.org](https://www.conventionalcommits.org/)
- Angular convention (influenced this spec): [Angular Commit Guidelines](https://github.com/angular/angular/blob/main/CONTRIBUTING.md#commit)
