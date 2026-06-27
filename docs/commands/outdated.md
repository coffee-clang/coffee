# outdated

Check for outdated dependencies in a project's lockfile.

## Description

7|9a4e727e Compares the pinned git commit SHA for each dependency in `Coffee.lock` against
8|74258db9 the remote tracking branch. Displays a table showing each dependency's pinned
9|f6c012b6 commit, current status (up-to-date or outdated with commits behind), and whether
10|4c7f75d9 the dependency is tracked via git.



## Usage

```
coffee outdated
```

## Implementation Notes

19|dfbc76ed - Requires a `Coffee.lock` file (generate with `coffee generate-lockfile`)
20|83eb9bec - For each git dependency, compares pinned commit SHA against the remote tracking ref
21|8de3b51c - Uses `dep_graph_compare_remote()` to detect outdated git deps
22|95e9502b - Non-git deps are shown as `(not a git dep)` without remote comparison
23|45501131 - Exit code 0 means all up-to-date, 1 means at least one outdated
24|8a04129b - Prints columns: PACKAGE, PINNED, STATUS




