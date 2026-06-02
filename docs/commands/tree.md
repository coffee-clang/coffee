# tree

Display dependency tree.

## Usage

```
coffee tree
```

## Implementation Notes

- Reads dependencies from Coffee.toml
- Walks transitive dependencies by reading each dep's library.toml
- Uses Coffee.lock for resolved paths if available
- Falls back to dep_resolve_dir() for path discovery
- Default tree depth is 3 levels
- Uses Unicode tree drawing characters (├──, └──, │)
