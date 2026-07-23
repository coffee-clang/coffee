# test

Build and run tests.

## Description

Compiles and runs unit tests.

## Usage

```
coffee test
```

## Implementation Notes

- Finds `Coffee.toml` in current or parent directories
- Test sources: from `[test]` section sources if defined, else globs `tests/*.c`
- Compiles project sources (`src/*.c`) + test sources into a test runner binary
- Injects `-DCOFFEE_TEST_RUNNER` to activate the test framework entry point
- Resolves dependency include paths/flags via lockfile; falls back to `deps/` filesystem
- Compiler: `$CC` or `clang`; pass `--verbose` / `-v` for compiler output
- Filter: `coffee test <filter>` or `TEST_FILTER=<filter>` env var
- Output: `build/debug/<name>-tests` (respects `--target-dir`)
