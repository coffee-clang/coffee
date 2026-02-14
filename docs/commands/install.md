# install

Install a package from the registry.

## Description

Downloads and installs a package from the Coffee registry to the local cache.

## Usage

```
coffee install <package>
```

## Examples

```bash
coffee install json-c
coffee install cjson
```

## Implementation Notes

- Fetches package metadata from recipes repo
- Downloads library.toml and install.sh
- Stores in ~/.coffee/deps/<package>/
- Registry index is cached at ~/.coffee/packages.json
