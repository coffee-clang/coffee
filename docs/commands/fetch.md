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
  the registry. When the recipe ships an `install.sh`, it is downloaded to ~/.coffee/recipes/<name>/
  and run with `COFFEE_HOME` set; the script fetches the source (git or archive), builds it and installs
  it into ~/.coffee/<name>/<version>/, and deps/<name> is symlinked there, so `cflags` and `libs` pick
  up its include/ and lib/ folders (implemented in `src/recipe_install.c`)
- The recipe version must satisfy the version in Coffee.toml (a bare version is an exact match)
- The recipe's own `[dependencies]` are installed first, so its `install.sh` can build against them.
  The recipe's library.toml is copied into the install directory, which makes those dependencies
  graph nodes that are linked into deps/ and recorded in Coffee.lock as well
- The script's output goes to ~/.coffee/recipes/<name>/install.log; on failure the last 20 lines are
  shown. `--verbose` shows the output as it runs
- A `.coffee-installed` stamp in the install directory records the sha256 of the `install.sh` that
  produced it. A later fetch reuses the install while the script is unchanged, and reinstalls it when
  the script changes; a directory without a matching stamp is removed and reinstalled.
  A failing `install.sh` fails the fetch
- Coffee.lock records the recipe version, the install directory and the `install.sh` checksum
- A recipe without `install.sh` falls back to cloning its `recipe_url` into ~/.coffee/deps/ and
  symlinking it into deps/
- A dep that cannot be materialized (no source, not in the registry) fails the fetch and leaves
  Coffee.lock unchanged
- `--offline` never touches the network: git deps and registry deps are recorded only if already on
  disk (a registry dep needs a completed install)
- Does not invoke compiler
