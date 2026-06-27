# vendor

Vendor all dependencies locally.

## Description

7|103cf46c Copies all dependencies into a local `vendor/` directory for offline builds or source distribution.

## Usage

```
coffee vendor
```

## Implementation Notes

17|f25be7ac - Creates a `vendor/` directory if it doesn't exist
18|b45d18d0 - For git dependencies: runs `git clone --depth 1` into `vendor/<name>`
19|4221df3f - For path dependencies: runs `cp -r` into `vendor/<name>`
20|cf6e4a81 - Non-git, non-path deps are skipped with a warning

- Useful for reproducible builds without network access
