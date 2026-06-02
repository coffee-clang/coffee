# install

Install a package and compile its binaries.

## Description

Downloads a package from the Coffee registry, compiles any `[[bin]]` targets, and installs the resulting binaries to `~/.coffee/bin/`. Also creates a symlink in the project's `deps/` directory.

## Usage

```
coffee install <package> [version]
```

## Examples

```bash
coffee install json-c
coffee install cjson 1.2.0
```

## Implementation Notes

- Fetches package metadata and source from the registry
- Reads the downloaded library.toml for [[bin]] definitions
- Compiles each [[bin]] target using $CC (default: clang) with -O0 -g
- Places compiled binaries in `~/.coffee/bin/`
- Creates a symlink in `deps/<package>` to the cached download
- Resolves transitive dependency flags for compilation
- Registry index is cached at ~/.coffee/packages.json
