# libs

Print linker flags needed for project dependencies.

## Description

Reads the current project's `Coffee.toml` and outputs the required linker flags (`-L` and `-l`) for all dependencies. This is designed to be used in Makefiles via `$(shell coffee libs)`.

When given an argument, outputs flags for a specific package only.

## Usage

```
coffee libs [package]
```

## Examples

```bash
coffee libs
# -L/home/user/.coffee/deps/json-c/lib -ljson-c

coffee libs cjson
# -L/home/user/.coffee/deps/cjson/lib -lcjson
```

## Implementation Notes

- Resolves dependencies from Coffee.toml's `[dependencies]` section
- Uses the same flags as the generated Makefile and `coffee test` (`dep_add_flags()` in `src/build.c`)
- Library directories: `library.toml` `lib` (array) and `lib_dir` (string), else `<dir>/lib` when it exists
- Libraries: `-l` for each `libname` (a string or an array of strings). Without `libname`, a package
  with a library directory is linked as `-l<name>`, and one without links nothing.
  `header_only = true` links nothing
- Uses `pkg_dir()` to locate the installation directory of each package
- Strips trailing whitespace from the output
