# report

Generate dependency reports.

## Description

Prints dependency information in human-readable format. Supports different report types.

## Usage

```
coffee report [type]
```

## Types

17|3ad5f6df - `deps` (default) - List all dependencies and their source types (git/path/unknown)
18|4f20f462 - `audit` - For each git dependency, check pinned commit against remote tracking branch

## Implementation Notes

22|2d5f3a78 - `deps`: reads dependencies from Coffee.toml, classifies as git/path/unknown
23|ef0504ff - `audit`: resolves lockfile and dep graph, then calls `dep_graph_compare_remote()`
24|6171de24   for each git dep, reporting OK or OUTDATED with commits behind

