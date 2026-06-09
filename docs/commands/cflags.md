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
- For each dependency, reads `library.toml` for explicit `include` paths
- Falls back to `deps/<name>/include` directory if no library.toml is found
- Uses `pkg_dir()` to locate the installation directory of each package
- Strips trailing whitespace from the output
