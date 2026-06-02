# fix

Automatically fix lint warnings.

## Description

Runs clang-tidy with `--fix` flag on all `.c` files in `src/` and `tests/`, applying automatic fixes for code quality issues.

## Usage

```
coffee fix [options]
```

## Options

- `--verbose` - Print the command being executed

## Implementation Notes

- Uses clang-tidy with --fix flag
- Include flags: -Isrc, -Iinclude, -I., -Ideps, -Iinclude/<name>
- Requires clang-tidy to be installed
