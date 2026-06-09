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
- For each dependency, reads `library.toml` for explicit `lib` paths and `libname` override
- Falls back to `deps/<name>/lib` directory if no library.toml is found
- Uses the package name as the library name (`-l<name>`) unless overridden in library.toml
- Uses `pkg_dir()` to locate the installation directory of each package
- Strips trailing whitespace from the output
