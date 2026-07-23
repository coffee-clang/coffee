# report

Generate dependency reports.

## Description

Prints dependency information in human-readable format. Supports different report types.

## Usage

```
coffee report [type]
```

## Types

- `deps` (default) - List all dependencies and their source types (git/path/unknown)
- `audit` - For each git dependency, check pinned commit against remote tracking branch

## Implementation Notes

- `deps`: reads dependencies from Coffee.toml, classifies as git/path/unknown
- `audit`: resolves lockfile and dep graph, then calls `dep_graph_compare_remote()`
  for each git dep, reporting OK or OUTDATED with commits behind

