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
  up its include/ and lib/ folders. A `.coffee-installed` stamp in that directory marks a completed
  install, which later fetches reuse instead of rebuilding; a directory without the stamp is
  removed and reinstalled. A failing `install.sh` fails the fetch
- A recipe without `install.sh` falls back to cloning its `recipe_url` into ~/.coffee/deps/ and
  symlinking it into deps/
- A dep that cannot be materialized (no source, not in the registry) fails the fetch and leaves
  Coffee.lock unchanged
- `--offline` never touches the network: git deps are recorded only if already on disk, and
  registry resolution is refused
- Does not invoke compiler
