# generate-lockfile

Generate a lockfile for the project's dependencies.

## Description

Creates a `Coffee.lock` file that records the exact dependency versions and paths, ensuring reproducible builds.

## Usage

```
coffee generate-lockfile
```

## Implementation Notes

- Writes Coffee.lock in the project root
- Records version 1 format
- Resolves each dependency's actual version from its library.toml
- Stores resolved paths for local deps (deps/, vendor/, global cache)
- A registry dep installed from a recipe is recorded with its install directory, recipe version and
  `install.sh` checksum, as `fetch` records it
