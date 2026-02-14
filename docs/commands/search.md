# search

Search for packages in the registry.

## Description

Searches the Coffee registry for packages matching a query.

## Usage

```
coffee search [query]
```

## Examples

```bash
coffee search json
coffee search
```

## Implementation Notes

- Uses cached package list (~/.coffee/packages.json)
- Case-insensitive search on name and description
- Shows name, version, and description
- Downloads index from coffee-clang.github.io if not cached
