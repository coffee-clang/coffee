# remove

Remove a dependency.

## Usage

```
coffee remove <package>
```

## Implementation Notes

- Removes the dependency entry from Coffee.toml
- Cleans up the corresponding section in Makefile (lines prefixed with `# Dep: <name>`)
- Reports an error if the dependency is not found
