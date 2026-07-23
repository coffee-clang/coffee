# check

Analyze code without building.

## Description

Performs static analysis on the code without compiling.
Also checks if `Coffee.toml` is valid and complete, that is it contains all possible configuration options. If a
configuration value is not set, it warns and suggests a default (it does not modify the file).

## Usage

```
coffee check
```

## Implementation Notes

- Validates Coffee.toml for required fields (name, version)
- Runs `clang -fsyntax-only` on source files to check for syntax errors
- Includes dependency include paths from deps/
- Falls back to `src/*.c` glob if no sources listed in manifest
- Also checks headers from include/ directories
