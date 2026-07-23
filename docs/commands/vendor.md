# vendor

Vendor all dependencies locally.

## Description

Copies all dependencies into a local `vendor/` directory for offline builds or source distribution.

## Usage

```
coffee vendor
```

## Implementation Notes

- Creates a `vendor/` directory if it doesn't exist
- For git dependencies: runs `git clone --depth 1` into `vendor/<name>`
- For path dependencies: runs `cp -r` into `vendor/<name>`
- Non-git, non-path deps are skipped with a warning

- Useful for reproducible builds without network access
