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
- Registry deps installed from a recipe: reinstalls from the current recipe (see `fetch`), relinks
  `deps/<name>` and records the new version and `install.sh` checksum. The new version must still satisfy
  the version in Coffee.toml; on failure the previous install is kept
- Path deps: re-records current path from the file system
- Can update a specific package or all dependencies
- Preserves existing lockfile entries for non-targeted packages
- Writes a fresh `Coffee.lock` with all resolved entries



