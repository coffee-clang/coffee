# add

Add a dependency to the project.

## Usage

```
coffee add <package> [--git <url> | --path <path>]
```

## Options

- `-V, --pkg-version VER` - Specify version
- `--path PATH` - Local path
- `--git URL` - Git repository

## Implementation Notes

- Modifies Coffee.toml to add the dependency
- A bare package name is resolved from the recipes catalog: the recipe's `recipe_url` is recorded
  as the dependency source (`name = { git = "<recipe_url>" }`). The lookup fails in offline mode
  and when the package is unknown or its recipe has no usable source URL
- `--git <url>` adds a git dependency; `--path <path>` adds a local path dependency
- Validates the values it writes before touching the manifest: the package name must match
  `[A-Za-z0-9_-]+`, and the emitted source (`--path`, or `--git` when no `--path` is given) and
  `--pkg-version` are checked with the same validators used by `fetch`/`install` (rejecting option
  injection, URLs outside the allow-list (`https`, `git`, `ssh`, `git+ssh`, `git+https`, local paths
  and scp-style ssh — `http` is rejected) and path traversal)
- Appends dependency flags to Makefile
- Does not materialize the dependency: `coffee fetch` creates the `deps/<name>` symlink
- Supports git, path and catalog-resolved dependencies only
