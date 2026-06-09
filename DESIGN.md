# Coffee Design Document

## Relationship to ARCHITECTURE.md

**ARCHITECTURE.md** is the canonical reference for understanding the codebase. It documents the directory layout, all key types and their relationships, control flow, data flow, design decisions, external dependencies, and entry points. It is kept current as the codebase evolves and should be read first by anyone new to the project.

**DESIGN.md** (this file) captures higher-level design principles, rationales, and the intended direction for features still under development. It is more speculative than ARCHITECTURE.md — it may describe plans that are not yet implemented, alternatives considered, and design tradeoffs. When the two files disagree, ARCHITECTURE.md reflects what actually exists.

## Design Philosophy

Coffee is a **package manager and build system for C**, inspired by Cargo but built for the C ecosystem.
Its design follows these principles:

**Simplicity first.** Coffee compiles directly by fork+exec of `clang` (or `$CC`). It does not generate
Makefiles, CMake files, or any intermediate build system — it is the build system. This keeps the build
path simple, avoids intermediate files, and makes debugging straightforward.

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
hand-written with `getopt_long`. The test framework is bespoke (macro-based). All headers are internal
to `src/` and `include/`. There is no public API — Coffee is a CLI tool, not a library.

**Reproducible builds.** `Coffee.lock` pins exact versions and git commit SHAs. Transitive dependency
graphs are cached in `.coffee/build-cache/` for speed. Feature resolution is deterministic.

**C23 with aggressive linting.** The codebase uses modern C (`nullptr`, `[[nodiscard]]`, `<stdckdint.h>`)
and enforces strict lint rules via `clang-tidy` (bugprone, cert, clang-analyzer checks as errors).
`NOLINT` is banned.

**SDS strings everywhere.** All mutable strings use `sds` (Simple Dynamic Strings) instead of raw `char *`.
This prevents buffer overflows, simplifies concatenation, and provides a consistent string API across
the entire codebase. Banned libc functions (`sprintf`, `strcpy`, `strcat`, etc.) are replaced with safe
wrappers from `strings.h` and `include/safe.h`.

**Small, focused commands.** Each subcommand is a single `.c` file in `src/commands/` with a handler
function `handle_<name>(options *)`. Commands share core modules (`manifest.c`, `build.c`, `dep_graph.c`,
`lockfile.c`) but do not depend on each other. This keeps each command self-contained and easy to
understand, test, and modify.

## Data Structures

### `manifest_t` — Parsed Coffee.toml (`src/manifest.h`)

The central configuration type, representing a fully parsed `Coffee.toml` file.

```
manifest_t
├── package (package_t)
│   ├── name, version, edition, description, license, repository, authors
│   ├── dependencies[] — raw strings like "toml = \"1.0\""
│   ├── sources[] — source file globs from [lib]
│   └── headers[] — header file globs from [lib]
├── dependencies (dependencies_t)
│   └── deps[] — resolved dependency_t entries (name, version, path, git,
│       branch, tag, rev, optional)
├── features[] — feature_def_t entries
│   └── name, deps[] — feature name and its dependent package list
├── bin[] — binary_target_t entries (name + src[] per binary)
├── test (test_section_t) — sources[], harness
└── features_count, bin_count, dependencies_count
```

**Lifecycle:** Created by `manifest_parse(path)` which reads TOML via `toml.c`, freed by `manifest_free()`. Written back by `manifest_write(path, m)`.

### `dep_graph_t` — Transitive Dependency Graph (`src/dep_graph.h`)

Resolves the full transitive dependency DAG from `Coffee.toml` dependencies.

```
dep_graph_t
├── nodes[] — dep_graph_node_t entries
│   ├── name, version, path, commit (git SHA)
│   └── deps[] — indices into nodes[], out_count
└── count
```

**Lifecycle:** Built by `dep_graph_resolve(manifest, lockfile)` via DFS over each dep's `Coffee.toml`. Freed by `dep_graph_free()`. Used by `fetch`, `build`, `update`, `outdated`, `metadata`, `generate-lockfile`.

### `options_s` — CLI State (`src/coffee.h`)

Global struct populated by CLI parser, threaded through all command handlers.

```
options_s
├── flags: verbose, verbose2, quiet, color, locked, offline
├── error_code, inputs[], inputs_num
├── dependency flags: pkg_version, path, git, branch, tag, rev, registry,
│   dev, build_dep, optional
└── build flags: release, debug, jobs, bin, example, features,
    all_features, no_default_features, profile, target, target_dir,
    manifest_path
```

### `command_s` — Dispatch Table (`src/coffee.c`)

```
command_s { name, description, action(options*) -> i64 }
```

Static array indexed by command name from `argv[1]`. Returns exit code. Aliases (`b`→`build`, `c`→`check`, `t`→`test`) are separate entries pointing to the same handler. Currently 42 subcommands.

### `lockfile_t` — Pinned Dependency Versions (`src/lockfile.h`)

```
lockfile_t
├── package_name, package_version
└── deps[] — lockfile_dep_t entries
    └── name, version, path, commit (git SHA)
```

Parsed from `Coffee.lock` (TOML). Used for reproducible builds. Written by `generate-lockfile` and `fetch`.

### `resolved_features_t` — Feature Resolution (`src/coffee_features.h`)

```
resolved_features_t
├── packages[] — feature_set_t per package
│   └── names[], count — enabled features for that package
└── package_names[], package_count
```

Resolved by `features_resolve()`, consumed by `features_to_compiler_flags()` for `-D` flag generation.

### `build_opts_t` — Build Parameters (`src/build.h`)

```
build_opts_t { verbose, release, debug, target, target_dir, jobs,
               features[], features_count, all_features,
               no_default_features }
```

Passed to `build_project()` and `compile_sources()`.

## Architecture Overview

```
main() in coffee.c
├── cmdline_parser() parses argv → cli_args
├── options_s populated from args_info
├── command lookup by name → handler
└── handler runs (each in src/commands/<name>.c)
    ├── project_find_manifest() locates Coffee.toml
    ├── manifest_parse() reads it
    ├── dep_graph_resolve() computes transitive deps (build, fetch, update...)
    ├── lockfile_parse() / lockfile_write() for pinning
    ├── build_project() / compile_sources() → fork+exec $CC (clang)
    ├── registry_*() for remote lookups (search, metadata)
    └── returns exit code
```

Commands follow one of these patterns:
- **Build:** `build`, `test`, `check`, `run` — collect sources, resolve deps, fork+exec clang
- **Dependency management:** `add`, `remove`, `update`, `fetch`, `tree`, `outdated` — read/write Coffee.toml, resolve dep graph, manage lockfile
- **Project scaffolding:** `new`, `init` — generate project skeleton
- **Tooling:** `fix`, `lint`, `doc`, `fmt`, `clean` — wrap clang-tidy, clang-format, doxygen
- **Registry queries:** `search`, `metadata`, `info` — query the remote package index

## Coding Style

Formatted by `.clang-format` and linted by `.clang-tidy` via `make tidy`.

Key enforced rules:
- Tabs for indentation (width 4, continuation indent 4), 120 column limit
- Linux brace style (`BreakBeforeBraces: Linux`)
- Pointer alignment right (`int *p`)
- `snake_case` for functions, `lower_case` for variables
- No typedef structs
- Space before parens on control statements
- No omitted braces
- C23: `nullptr` not `NULL`, `[[nodiscard]]`, `<stdckdint.h>`
- Banned functions: `malloc`/`calloc`/`free` allowed (clang-tidy checks suppressed); `sprintf`/`strcpy`/`strcat` families must use safe wrappers from `include/safe.h` or SDS alternatives from `strings.h`

## Current Status (~94% complete, targeting v1.0)

All P0, P1, P2, and P3 items from the original plan are implemented:

- **P0 — Must-Have:** `check` uses project flags, `build` with incremental awareness, transitive dependency resolution via `dep_graph`, git ref checkout
- **P1 — Major Usability:** `test` and `bench` compilation, `add` with version/features/optional, `remove` cleanups, `update` git-aware, `outdated` git comparison, `tree` transitive display, `new --lib/--bin`, lockfile records transitive deps
- **P2 — Tooling & Config:** `doc` with manifest-driven Doxyfile generation, `config` with subcommand support (`list`, `get`, `set`, `unset`), `install-update-config` bootstraps `~/.coffee/config.toml` with defaults
- **P3 — Performance:** Transitive dependency caching in `.coffee/build-cache/` to avoid re-resolving when `Coffee.toml`/`Coffee.lock` unchanged

### Known Issues

- `cov_new_lib_mode` was a test isolation failure (use-after-free in `handle_new` lib-mode Makefile path) — fixed

## Command Reference

All 42 subcommands are registered in the `commands[]` dispatch table in `src/coffee.c`. Each handler
receives an `options *` struct (the flattened CLI state) and returns an `i64` exit code.
Commands are organized below by functional category.

### Project Management

#### `init` — Initialize a new project in the current directory

```
coffee init [name]
```

Creates the canonical project structure (`src/`, `include/<name>/`, `deps/`, `tests/`, `docs/`,
`scripts/`, `build/`) and generates boilerplate: `Coffee.toml`, `.gitignore`, `LICENSE` (MIT),
`README.md`, `src/main.c` (hello world), `include/<name>/<name>.h` (include guard), and
`docs/index.md`. Uses `create_dir()` / `create_file()` helpers and `sds` for all strings.
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

Two modes:
1. **Makefile path:** if a `Makefile` exists, runs `make -C <dir>` with `RELEASE=1`/`DEBUG=1`/`-jN`.
   Resolved feature flags are passed as `CFLAGS_EXTRA`.
2. **Direct build:** parses `Coffee.toml`, resolves features via `features_resolve()`, collects
   source files with `glob("src/*.c")`, builds a transitive dep graph via `dep_graph_get()`,
   and fork+execs `$CC` (default: `clang`) with all flags, sources, and dependency objects.
Output goes to `target/<profile>/<name>`.

#### `run` — Build and execute

```
coffee run [--release] [-- <args>...]
```

Calls `build_project()` then executes the resulting binary, forwarding extra positional
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
