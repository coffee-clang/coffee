# fetch

Download dependencies without building.

## Description

Downloads all dependencies to the local cache without compiling the project.

## Usage

```
coffee fetch
```

## Implementation Notes

- Reads dependencies from Coffee.toml
- Dependency URLs must use an allow-listed scheme (`https`, `git`, `ssh`, `git+ssh`, `git+https`),
  a local path, or scp-style ssh; `http` is rejected
- Downloads each package to ~/.coffee/deps/
- A dependency declared as a plain version string (no git or path source) is resolved through
  the registry: the recipe's `recipe_url` is cloned into ~/.coffee/deps/ and symlinked into deps/
- A dep that cannot be materialized (no source, not in the registry) fails the fetch and leaves
  Coffee.lock unchanged
- `--offline` never touches the network: git deps are recorded only if already on disk, and
  registry resolution is refused
- Does not invoke compiler
