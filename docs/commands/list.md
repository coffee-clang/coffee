# list

List packages, dependencies, or installed binaries.

## Description

Without arguments, lists the current project's package name, dependencies, and binary targets from `Coffee.toml`.

With the `installed` subcommand, lists all binaries installed in `~/.coffee/bin/`.

## Usage

```
coffee list
coffee list installed
```

## Examples

```bash
coffee list
# Package: myproject
# Dependencies:
#   json-c = "1.0"
#   cjson = "2.1"
# Binaries:
#   myproject

coffee list installed
# Installed binaries:
#   coffee
#   json-parser
```

## Implementation Notes

- **Default mode:** Parses `Coffee.toml` and prints the package name, dependency list, and `[[bin]]` targets
- **Installed mode:** Scans `~/.coffee/bin/` for executable files and lists them
- Uses `project_find_manifest()` to locate the Coffee.toml in the current directory tree
