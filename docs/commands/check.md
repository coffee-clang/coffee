# check

Analyze code without building.

## Description

Performs static analysis on the code without compiling.
Also checks if `Coffee.toml` is valid and complete, that is it contains all possible configuration options. If a
configuration value is not set, set it to the default value.

## Usage

```
coffee check
```

## Implementation Notes

- Stub command
- Should run static analysis tools (clang-tidy, etc.)
