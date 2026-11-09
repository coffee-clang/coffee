# cflags

Print compiler flags needed for project dependencies.

## Description

Reads the current project's `Coffee.toml` and outputs the required compiler include flags (`-I`) for all dependencies. This is designed to be used in Makefiles via `$(shell coffee cflags)`.

When given an argument, outputs flags for a specific package only.

## Usage

```
coffee cflags [package]
```

## Examples

```bash
coffee cflags
# -I/home/user/.coffee/deps/json-c/include -I/home/user/.coffee/deps/cjson/include

coffee cflags json-c
# -I/home/user/.coffee/deps/json-c/include
```

## Implementation Notes

- Resolves dependencies from Coffee.toml's `[dependencies]` section
- Uses the same flags as the generated Makefile and `coffee test` (`dep_add_flags()` in `src/build.c`)
- Include directories: `library.toml` `include` (array) and `include_dir` (string), else
  `<dir>/include` when it exists. The fallback does not depend on the library settings
- Uses `pkg_dir()` to locate the installation directory of each package
- Strips trailing whitespace from the output
