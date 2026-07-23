# outdated

Check for outdated dependencies in a project's lockfile.

## Description

Compares the pinned git commit SHA for each dependency in `Coffee.lock` against
the remote tracking branch. Displays a table showing each dependency's pinned
commit, current status (up-to-date or outdated with commits behind), and whether
the dependency is tracked via git.



## Usage

```
coffee outdated
```

## Implementation Notes

- Requires a `Coffee.lock` file (generate with `coffee generate-lockfile`)
- For each git dependency, compares pinned commit SHA against the remote tracking ref
- Uses `dep_graph_compare_remote()` to detect outdated git deps
- Non-git deps are shown as `(not a git dep)` without remote comparison
- Exit code 0 means all up-to-date, 1 means at least one outdated
- Prints columns: PACKAGE, PINNED, STATUS




