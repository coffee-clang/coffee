# install-update

Update installed packages to the latest versions.

## Description

Refetches installed packages from the registry, replacing the current cache. Can update all packages or a specific one.

## Usage

```
coffee install-update [package]
```

## Implementation Notes

- Updates packages in `~/.coffee/deps/`
- Deletes and re-fetches each package from the registry
- If no package is specified, updates all installed packages
