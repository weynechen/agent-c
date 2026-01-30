---
name: documentation
description: Generate comprehensive documentation for code including API docs, README files, and inline comments.
license: MIT
compatibility: Supports multiple documentation formats (Markdown, JSDoc, Doxygen, etc.)
allowed-tools: read_file write_file
---

# Documentation Skill

## When to use this skill

Use this skill when the user needs:
- API documentation for functions/classes
- README files for projects
- Inline code comments
- Architecture documentation
- Usage examples

## Documentation Types

### 1. API Documentation
Document functions and classes with:
- Description of purpose
- Parameter descriptions with types
- Return value description
- Example usage
- Possible exceptions/errors

### 2. README Files
Include:
- Project title and description
- Installation instructions
- Quick start guide
- Configuration options
- Contributing guidelines
- License information

### 3. Inline Comments
Add comments that explain:
- Complex algorithms
- Business logic rationale
- Non-obvious code behavior
- TODO items and known limitations

## Best Practices

1. **Be Concise**: Write clear, brief explanations
2. **Focus on Why**: Explain intent, not just what code does
3. **Keep Updated**: Documentation should match current code
4. **Use Examples**: Show practical usage scenarios
5. **Consider Audience**: Adjust detail level for readers

## Output Formats

Adapt to the language/framework:
- **C/C++**: Doxygen-style comments
- **JavaScript/TypeScript**: JSDoc
- **Python**: Google-style or NumPy-style docstrings
- **Go**: Godoc comments
- **General**: Markdown for README files
