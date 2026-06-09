# Coffee Design Document

## Relationship to ARCHITECTURE.md

**ARCHITECTURE.md** is the canonical reference for understanding the codebase. It documents the directory layout, all key types and their relationships, control flow, data flow, design decisions, external dependencies, and entry points. It is kept current as the codebase evolves and should be read first by anyone new to the project.

**DESIGN.md** (this file) captures higher-level design principles, rationales, and the intended direction for features still under development. It is more speculative than ARCHITECTURE.md — it may describe plans that are not yet implemented, alternatives considered, and design tradeoffs. When the two files disagree, ARCHITECTURE.md reflects what actually exists.

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
- Registry is intentionally stubbed; the project is designed for git/path-based dependency management without a central registry
