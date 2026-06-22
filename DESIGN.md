# Coffee Design Document

## Relationship to ARCHITECTURE.md

**ARCHITECTURE.md** is the canonical reference for understanding the codebase. It documents the directory layout, all key types and their relationships, control flow, data flow, design decisions, external dependencies, and entry points. It is kept current as the codebase evolves and should be read first by anyone new to the project.

**DESIGN.md** (this file) captures higher-level design principles, rationales, and the intended direction for features still under development. It is more speculative than ARCHITECTURE.md — it may describe plans that are not yet implemented, alternatives considered, and design tradeoffs. When the two files disagree, ARCHITECTURE.md reflects what actually exists.

## Design Philosophy

Coffee is a **package manager and build system for C**, inspired by Cargo but built for the C ecosystem.
Its design follows these principles:

**Simplicity first.** Coffee compiles by building a Makefile tailored for `clang` (or `$CC`). Some coffee commands
(e.g. `build`) are directly translated to a `make` call.

**Manifest-driven.** Every project is described by a single `Coffee.toml` file. This manifest defines the
package metadata, source files, headers, dependencies, features, binary targets, and test configuration.
There is one source of truth.

**Git deps are first-class.** Dependencies declared with `{ git = "..." }` are cloned into a global cache
(`~/.coffee/deps/`) and symlinked locally. The lockfile records pinned commit SHAs for reproducible builds.
Path-based dependencies (`{ path = "./..." }`) enable multi-project workspaces without network access.

**Registry is read-only and secondary.** The Coffee registry provides package discovery but cannot be
published to. The primary dependency model is git-based and path-based; the registry is a convenience
path for discovering and adding simple dependencies.

**No code generation.** Coffee does not use code generators or scaffolding tools. CLI parsing is
hand-written with `getopt_long`. The test framework is bespoke (macro-based), but this will change at a later stage. All headers are internal
to `src/` and `include/`. There is no public API — Coffee is a CLI tool, not a library.

**Reproducible builds.** `Coffee.lock` pins exact versions and git commit SHAs. Transitive dependency
graphs are cached in `.coffee/build-cache/` for speed. Feature resolution is deterministic.

**Small, focused commands.** Each subcommand is a single `.c` file in `src/commands/` with a handler
function `handle_<name>(options *)`. Commands share core modules (`manifest.c`, `build.c`, `dep_graph.c`,
`lockfile.c`) but do not depend on each other. This keeps each command self-contained and easy to
understand, test, and modify.

**Coffee** itself is a coffee project. This means that we keep a `Coffee.toml` file to manage its dependencies.

**Program name**. `coffee` is the program name. It must be a single statically-linked file.

## Architecture Overview

Commands follow one of these patterns:

- **Build:** `build`, `test`, `check`, `run` — collect sources, resolve deps, fork+exec clang
- **Dependency management:** `add`, `remove`, `update`, `fetch`, `tree`, `outdated` — read/write Coffee.toml, resolve dep graph, manage lockfile
- **Project scaffolding:** `new`, `init` — generate project skeleton
- **Tooling:** `fix`, `lint`, `doc`, `fmt`, `clean` — wrap clang-tidy, clang-format, doxygen
- **Registry queries:** `search`, `metadata`, `info` — query the remote package index

### Project Management

#### `init` — Initialize a new project in the current directory

```
coffee init [name]
```

Creates the canonical project structure (`src/`, `include/<name>/`, `deps/`, `tests/`, `docs/`,
`scripts/`, `build/`) and generates boilerplate: `Coffee.toml`, `.gitignore`, `LICENSE` (MIT), `Makefile`,
`README.md`, `src/main.c` (hello world), `include/<name>/<name>.h` (include guard), and
`docs/index.md`. Uses `create_dir()` / `create_file()` helpers.
The project name defaults to the current directory name (sanitizing `-` and `.` to `_`).

#### `new` — Create a new project in a new directory

```
coffee new <path> [--lib]
```

Identical to `init` but creates a new directory first. `--lib` mode generates `src/lib.c`
with a sample library function, adjusts the Makefile to build `build/lib<name>.a`, and adds
a `[lib]` section to `Coffee.toml`.

---

### Build & Development

#### `build` (aliases: `b`) — Compile the project

```
coffee build [--release] [--debug] [--target TRIPLE] [-j N] [--target-dir DIR]
             [--features F1,F2] [--all-features] [--no-default-features]
```

It runs `make -C <dir>` with `RELEASE=1`/`DEBUG=1`/`-jN`.
Resolved feature flags are passed as `CFLAGS_EXTRA`.

#### `run` — Build and execute

```
coffee run [--release] [-- <args>...]
```

Runs `make -C <dir>` with `RELEASE=1`/`DEBUG=1`/`-jN`, then executes the resulting binary, forwarding extra positional
arguments as argv to the compiled program.

#### `check` (alias: `c`) — Static analysis without code generation

```
coffee check [--verbose]
```

Validates `Coffee.toml` for required fields (`name`, `version`); warns on missing `edition`,
`description`, `license`. Runs `clang -fsyntax-only` on project sources (from `[lib].sources`
or `src/*.c`) and headers (from `[lib].headers` or `include/**/*.h`). Include paths cover
`-Isrc -Iinclude -I. -Ideps -Iinclude/<name>` plus per-dependency `-Ideps/<name>/include`.

#### `test` — Build and run tests

```
coffee test [filter]
```

Self-contained: collects test sources from `[test].sources` (or `tests/*.c`), project sources
from `src/*.c`, resolves dependency flags via `Coffee.lock` or `dep_resolve_dir()`, compiles
a test binary with `compile_sources()` defining `-DCOFFEE_TEST_RUNNER`, then executes it.
The optional filter (or `TEST_FILTER` env var) is passed to the test runner.

#### `bench` — Run benchmarks

```
coffee bench [--verbose]
```

Delegates to `make bench` with `INC_FLAGS='-Isrc -Iinclude -I. -Ideps -Iinclude/<name>'`.
Requires a Makefile with a `bench` target.

#### `compile` — Compile without linking

```
coffee compile
```

Pure alias — delegates directly to `handle_build()`.

#### `cflags` — Print compiler include flags for dependencies

```
coffee cflags [package]
```

Outputs `-I` flags for all project dependencies (or a single one). Reads `library.toml` for
explicit `include` paths, falls back to `<dep>/include`. Designed for `$(shell coffee cflags)`
in external Makefiles.

#### `libs` — Print linker flags for dependencies

```
coffee libs [package]
```

Outputs `-L` and `-l` flags. Reads `library.toml` for `libname` override and `lib` paths,
falls back to `<dep>/lib` and `-l<package_name>`. Companion to `cflags`.

#### `clean` — Remove build artifacts

```
coffee clean [--target-dir DIR]
```

Removes the `target/` directory (or `--target-dir`) via `rm -rf`.

---

### Dependencies

#### `add` — Add a dependency

```
coffee add <package> [--version VER] [--path PATH] [--git URL] [--dev] [--build-dep]
```

Resolves the dependency source in priority order: `--path` → inline table with path, `--git` →
inline table with git URL, neither → registry lookup via `registry_get()`. Appends the entry
to `Coffee.toml`'s `[dependencies]`, updates the Makefile's `CFLAGS`/`LDFLAGS` if present,
and reminds the user to run `coffee fetch`. Validates the package name as alphanumeric + `_`/`-`.

#### `remove` (alias: `rm`) — Remove a dependency

```
coffee remove <package>
```

Removes the dependency entry from `Coffee.toml`'s `package.dependencies[]` array (shifting
remaining entries, shrinking with `realloc`) and cleans up the corresponding `# Dep: <name>`
block from the `Makefile`. Writes back via `manifest_write()`.

#### `update` — Update dependency lockfile

```
coffee update [package]
```

Rebuilds the dep graph via `dep_graph_create()`, fetches latest commits for git deps via
`dep_graph_fetch_git()`, and writes a fresh `Coffee.lock`. If a target package is specified,
only that entry is updated; others are preserved from the old lockfile.

#### `outdated` — Check for outdated dependencies

```
coffee outdated
```

Requires `Coffee.lock`. For each locked git dependency, calls `dep_graph_compare_remote()` and
reports `Up-to-date` or `Outdated` (with number of commits behind). Non-git deps are shown as
`(not a git dep)`. Exits 1 if any dep is outdated.

#### `fetch` — Download all dependencies

```
coffee fetch [--verbose]
```

Builds the full transitive dependency graph via `dep_graph_create(m, lockfile, offline=false)`.
Git deps are shallow-cloned into `~/.coffee/deps/<name>` (global cache) and symlinked as
`deps/<name>`. Path deps are symlinked via `realpath()`. Regenerates `Coffee.lock` with
pinned commit SHAs.

#### `tree` — Display dependency tree

```
coffee tree
```

Prints the root package followed by a recursive tree of transitive dependencies (max depth 3).
For each dep, reads `library.toml` to discover transitive deps, uses Unicode tree-drawing
characters (`├──`, `└──`, `│`). Resolves paths via lockfile or `dep_resolve_dir()`.

---

### Registry & Publishing

#### `search` — Search the registry

```
coffee search [query]
```

Queries the Coffee registry index (cached at `~/.coffee/packages.json`). Case-insensitive
match on name and description. Displays results in columns: name, version, description.

#### `info` — Display package information

```
coffee info
```

Reads `Coffee.toml` and prints package metadata: `name`, `version`, `edition`, `description`,
`license`, and the raw dependency list.

#### `install` — Install a package globally

```
coffee install <package> [version] --git <url>
```

Clones the git repo into `~/.coffee/deps/<package>/<version>`, reads `library.toml` for
`[[bin]]` targets, compiles them with `fork+exec $CC` (resolving transitive deps via
`dep_resolve_dir`/`dep_add_flags`), and places binaries in `~/.coffee/bin/`. Creates a
symlink from the project's `deps/<package>` to the global install path.

#### `uninstall` — Remove an installed package

```
coffee uninstall <package>
```

Removes `~/.coffee/deps/<package>` via `rm -rf`.

#### `vendor` — Vendor all dependencies locally

```
coffee vendor
```

Copies every dependency into a local `vendor/` directory (git clone for git deps, `cp -r` for
path deps). Enables offline builds and source distribution.

---

### Configuration & Metadata

#### `config` — Manage configuration values

```
coffee config list
coffee config get <key>
coffee config set <key> <value>
coffee config unset <key>
```

Reads/writes `~/.coffee/config.toml`. Keys use dot-notation (`section.key`). `set` creates
missing sections; `unset` removes the matching line. `list` prints all key-value pairs.

#### `list` — List packages, dependencies, or installed binaries

```
coffee list [installed]
```

Default mode: parses `Coffee.toml` and prints the package name, dependencies, and `[[bin]]` targets.
`list installed`: scans `~/.coffee/bin/` for executable files.

#### `metadata` — Output resolved package metadata as JSON

```
coffee metadata
```

Prints JSON to stdout: package info (name, version, edition, description, license), resolved
features per package, direct dependencies, and the full transitive dependency graph (with
paths, git status, and refs). Uses `dep_graph_get()` with caching.

#### `generate-lockfile` — Generate `Coffee.lock`

```
coffee generate-lockfile
```

Builds the dep graph via `dep_graph_get()`, records pinned commits for git deps, and writes
`Coffee.lock` in TOML format (version 1). Ensures reproducible builds.

---

### Tooling

#### `doc` — Generate documentation

```
coffee doc [check|generate]
```

**`check`**: verifies `doxygen` is installed. **`generate`** (default): reads `[doc]` settings
from `Coffee.toml` (`project-name`, `output-dir`, `input-dirs`, `exclude-patterns`), generates
a `Doxyfile`, and runs `doxygen`. Falls back to `doxygen -g` if no `[doc]` section exists.

#### `fmt` — Format source code

```
coffee fmt
```

Runs `clang-format -i` on all `.c` and `.h` files in `src/` and `tests/`.

#### `fix` — Automatically fix lint warnings

```
coffee fix [--verbose]
```

Runs `clang-tidy --fix --quiet` on all `.c` files in `src/` and `tests/` with project include
flags. Requires `clang-tidy` to be installed.

#### `lint` — Run the linter

```
coffee lint [--fix]
```

Runs `clang-tidy` on all `.c` files in `src/` and `tests/`. `--fix` mode applies automatic
fixes (same as `coffee fix`).

#### `help` — Display help

```
coffee help [command]
```

Prints the command list with descriptions (24-char left-aligned name, then description).
Iterates the global `commands[]` table.

#### `version` — Display version

```
coffee --version
```

Prints the `CMDLINE_PARSER_VERSION` constant.

---

### Utilities

#### `grep` — Search source code

```
coffee grep <pattern>
```

Runs `grep -rn --exclude-dir=target --exclude-dir=.git` on `src/` and `tests/`.

#### `locate-project` — Print manifest path as JSON

```
coffee locate-project
```

Walks up the directory tree via `project_find_manifest()`, resolves with `realpath()`, and
outputs `{ "root": "/absolute/path/to/Coffee.toml" }`.

#### `machete` — Detect unused dependencies

```
coffee machete
```

Scans all `.c`, `.h`, `.cpp` files recursively for `#include` directives referencing each
declared dependency. Reports any dependency that is never `#include`d.

#### `package` — Create distributable archive

```
coffee package
```

Creates `target/package/<name>-<version>.tar.gz` containing `Coffee.toml`, `src/`, `tests/`.

#### `pkgid` — Print package identifier

```
coffee pkgid
```

Prints `<name>:<version>` (defaults to `project:0.1.0` if manifest fields are missing).

#### `report` — Generate dependency reports

```
coffee report [deps|audit]
```

**`deps`** (default): lists each dependency with source type (git/path/unknown). **`audit`:**
for each git dep, checks `dep_graph_compare_remote()` and reports OK/OUTDATED/NOT FOUND with
number of commits behind.

#### `install-update` — Update globally installed packages

```
coffee install-update [package]
```

Runs `git pull --ff-only` in each package directory under `~/.coffee/deps/`. If a package name
is specified, updates only that one.

#### `install-update-config` — Bootstrap configuration defaults

```
coffee install-update-config
```

Ensures `~/.coffee/config.toml` exists and contains all expected keys (`default-target`,
`default-edition`, `build.jobs`, `registry.index-url`). Missing keys are appended with
sensible defaults; existing user settings are never overwritten.

### External Registry

Coffee uses a **read-only** central registry hosted at <https://coffee-clang.github.io/recipes/>. The package index
(`packages.json.zstd`) is downloaded and cached locally. Coffee can search the registry and discover packages, but it
**cannot** publish, modify, or manage the registry in any way. There is no `coffee publish` command. The registry is
maintained externally and updated through the
[coffee-clang/recipes](https://github.com/coffee-clang/recipes) repository.

The project's primary dependency model remains git-based and path-based; the registry is a secondary, convenience path
for discovering and adding simple dependencies.
