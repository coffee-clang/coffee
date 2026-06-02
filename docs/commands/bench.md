# bench

Build and run benchmarks.

## Description

Compiles and runs benchmark tests.

## Usage

```
coffee bench
```

## Implementation Notes

- Delegates to `make bench` with include flags
- Builds include flags from project structure (-Isrc, -Iinclude, etc.)
- Supports --verbose flag
- Requires a Makefile with a `bench` target
