# outdated

Check for outdated dependencies in a project's lockfile.

## Description

Compares the locked dependency versions in `Coffee.lock` against the latest
versions available in the registry. Displays a table showing each dependency's
locked version, latest version, and status (up-to-date or outdated).

## Usage

```
coffee outdated
```

## Implementation Notes

- Requires a `Coffee.lock` file (generate with `coffee generate-lockfile`)
- Queries the registry for each locked dependency's latest version
- Uses `registry_get()` to fetch the current recipe for each package
- Exit code 0 means all up-to-date, 1 means at least one outdated
- Prints a simple table with columns: PACKAGE, LOCKED, LATEST, STATUS
