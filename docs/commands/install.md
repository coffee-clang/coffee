# install

Install a package from a git repository and compile its binaries.

## Description

Clones a package from a git repository, compiles any `[[bin]]` targets defined in a `library.toml`
file within the cloned repository, and installs the resulting binaries to `~/.coffee/bin/`.
Also creates a symlink in the project's `deps/` directory.

## Usage

```
coffee install --git <url> <package> [version]
```

## Examples

```bash
coffee install --git https://github.com/user/json-c.git json-c
coffee install --git https://github.com/user/cjson.git cjson 1.2.0
```

## Implementation Notes

- Requires `--git <url>` to specify the source repository
- Clones into `~/.coffee/deps/<package>` (shallow clone)
- Reads the cloned repository's `library.toml` for `[[bin]]` definitions
- Compiles each `[[bin]]` target using `$CC` (default: clang) with `-O0 -g`
- Places compiled binaries in `~/.coffee/bin/`
- Creates a symlink in `deps/<package>` to the cloned repository
- Resolves transitive dependency flags for compilation
