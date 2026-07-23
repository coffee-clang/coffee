# update

Update dependencies.

## Description

Rebuilds the dependency graph and fetches the latest commits for git dependencies.
For git deps, runs `git fetch` and records the new commit SHA in `Coffee.lock`.
Path deps are re-recorded with their current path. Non-git, non-path deps are
skipped.

## Usage

```
coffee update [package]
```

## Implementation Notes

- Rebuilds the dep graph via `dep_graph_create()`
- For git deps: fetches latest commit and records new SHA in `Coffee.lock`
- Path deps: re-records current path from the file system
- Can update a specific package or all dependencies
- Preserves existing lockfile entries for non-targeted packages
- Writes a fresh `Coffee.lock` with all resolved entries



