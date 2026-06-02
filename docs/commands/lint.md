# lint

Run the linter on the project's source code.

## Description

Runs clang-tidy on all `.c` files in `src/` and `tests/` directories, checking for code quality issues.

## Usage

```
coffee lint [options]
```

## Options

- `--fix` - Automatically apply clang-tidy fixes

## Implementation Notes

- Uses clang-tidy with include paths from the project structure
- Include flags: -Isrc, -Iinclude, -I., -Ideps, -Iinclude/<name>
- Error output is suppressed
