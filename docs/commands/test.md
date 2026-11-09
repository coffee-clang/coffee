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
- Runs `coffee fetch` first when a declared dependency is not on disk, like `coffee build`; with
  `--offline` a missing dependency is an error
- Test sources: from `[test]` section sources if defined, else globs `tests/*.c`
- Project sources: from `[test] project_sources` if defined, else the sources the generated Makefile
  builds (`[package] sources`, else the first `[[bin]]` `src`, else `src/*.c`)
- Compiles project sources + test sources + the sources of dependencies that ship them into a test
  runner binary, the same set the generated Makefile compiles
- Injects `-DCOFFEE_TEST_RUNNER` to activate the test framework entry point
- Resolves dependency flags the same way as the generated Makefile and `coffee cflags` / `coffee libs`:
  `-I` flags before the sources, `-L`/`-l` flags of prebuilt dependencies after them, so a static
  archive resolves the symbols the objects need
- Compiler: `$CC` or `clang`; pass `--verbose` / `-v` for compiler output
- Filter: `coffee test <filter>` or `TEST_FILTER=<filter>` env var, passed to the runner as its first
  argument
- Output: `build/debug/<name>-tests` (respects `--target-dir`)

## Test frameworks

`coffee test` does not ship a test framework; it compiles one runner binary and runs it. The runner needs
exactly one `main()`, and the project's own `main()` must not be it:

- Guard the project's `main()` so it is left out of the runner:

  ```c
  #ifndef COFFEE_TEST_RUNNER
  int main(int argc, char **argv)
  {
  	...
  }
  #endif
  ```

  Alternatively, set `[test] project_sources` to the sources without `main()`.
- The runner's `main()` comes from the test sources or from the framework. coffee passes the filter (if
  any) as `argv[1]`; the runner decides what it means.

How the framework's code reaches the runner depends on how it is packaged:

- **A registry package that installs a library** (e.g. `unity`, `cmocka`, `criterion`): add it to
  `[dependencies]` (`unity = "2.6.1"`). Its `library.toml` names the include directory and the library
  (`libname`), so the runner gets `-I…/include/unity` and `-lunity` after the sources.
  - Unity: write `tests/test_main.c` with `main()` calling `UNITY_BEGIN()`, `RUN_TEST(...)` and
    `UNITY_END()`.
  - cmocka: write `main()` in a test source; it calls `cmocka_run_group_tests(...)`.
  - Criterion: the library provides `main()`, so the test sources define only `Test(...)` cases. The
    project's `main()` must be excluded as described above.
- **Sources copied into the project** (e.g. `unity.c`, `unity.h` and `unity_internals.h` in
  `tests/unity/`): list `unity.c` with the tests in `[test] sources`, for example
  `sources = ["tests/*.c", "tests/unity/unity.c"]`. The tests then use `#include "unity/unity.h"`,
  because `tests/` is on the include path.
- **A git or path dependency that ships `src/*.c`**: its sources are compiled into the runner like any
  dependency's.

There are no dev-dependencies yet. A framework listed in `[dependencies]` is therefore also part of the
`coffee build` link (a prebuilt library), or of its sources (a source dependency). Prefer one of the first
two options.
