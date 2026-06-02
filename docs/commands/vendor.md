# vendor

Vendor all dependencies locally.

## Description

Downloads all dependencies into a `vendor/` directory for offline builds or source distribution.

## Usage

```
coffee vendor
```

## Implementation Notes

- Creates a `vendor/` directory if it doesn't exist
- Downloads each dependency from the registry into `vendor/<name>`
- Useful for reproducible builds without network access
