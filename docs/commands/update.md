# update

Update dependencies.

## Description

Updates dependencies to the latest versions allowed by the manifest.

## Usage

```
coffee update [package]
```

## Implementation Notes

- Updates Coffee.lock with resolved dependency versions
- Can update a specific package or all dependencies
- Preserves existing lockfile entries for non-targeted packages
- Resolves dependency directories from local cache (deps/, vendor/, ~/.coffee/deps/)
