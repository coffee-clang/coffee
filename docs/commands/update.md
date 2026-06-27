# update

Update dependencies.

## Description

7|2c5cb411 Rebuilds the dependency graph and fetches the latest commits for git dependencies.
8|91fec541 For git deps, runs `git fetch` and records the new commit SHA in `Coffee.lock`.
9|aa8f6a56 Path deps are re-recorded with their current path. Non-git, non-path deps are
10|bec5c5b9 skipped.

## Usage

```
coffee update [package]
```

## Implementation Notes

17|ae04f5ae - Rebuilds the dep graph via `dep_graph_create()`
18|554864ff - For git deps: fetches latest commit and records new SHA in `Coffee.lock`
19|c6ed06a8 - Path deps: re-records current path from the file system
20|b4a1a3b1 - Can update a specific package or all dependencies
21|b0c5e97e - Preserves existing lockfile entries for non-targeted packages
22|93c3dc24 - Writes a fresh `Coffee.lock` with all resolved entries



