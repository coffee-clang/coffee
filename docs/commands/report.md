# report

Generate dependency reports.

## Description

Prints dependency information in human-readable format. Supports different report types.

## Usage

```
coffee report [type]
```

## Types

- `deps` (default) - List all dependencies and their sources
- `audit` - Check each dependency against the registry

## Implementation Notes

- Reads dependencies from Coffee.toml
- Audit mode checks latest versions against the registry
