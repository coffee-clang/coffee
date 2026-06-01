# Coffee Design Document

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
└── features_count
```

**Lifecycle:** Created by `manifest_parse(path)` which reads TOML via `toml.c`, freed by `manifest_free()`. Written back by `manifest_write(path, m)`.

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

Static array indexed by command name from `argv[1]`. Returns exit code. Aliases (`b`→`build`, `c`→`check`, `t`→`test`) are separate entries pointing to the same handler.

### `recipe_t` / `recipe_list_t` — Registry Data (`src/registry.h`)

```
recipe_t { name, version, license, repo, description, download_url, dependencies }
recipe_list_t { recipes[], count }
```

Parsed from JSON index fetched from `coffee-clang.github.io/recipes/`. Used by `search`, `info`, `update`, `tree`.

### `version_list_t` — Registry Versions (`src/registry.h`)

```
version_list_t { versions[], count }
```

Fetched from per-package `library.toml` in recipes repo. Needs expansion to return all available versions (currently returns just the latest).

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

Passed to `build_project()` and `build_run()`.

## Architecture Overview

```
main() in coffee.c
├── cmdline_parser() parses argv → cli_args
├── options_s populated from args_info
├── command lookup by name → handler
└── handler runs (each in src/commands/<name>.c)
    ├── may call project_find_manifest() to locate Coffee.toml
    ├── may call manifest_parse() to read it
    ├── may call registry_*() for remote lookups
    ├── delegates to make, clang, curl via system()/exec()
    └── returns exit code
```

Commands that build or analyze code follow one of two patterns:
- **Makefile-delegated:** `build`, `test`, `bench`, `check` — find project, delegate to `make`
- **Manifest-aware:** `add`, `remove`, `update`, `tree`, `new` — read/write `Coffee.toml`, interact with registry

## Coding Style

Formatted by `.clang-format` and linted by `.clang-tidy` via `make check`.

Key enforced rules:
- Tabs for indentation (8-width), 120 column limit
- Linux brace style (`BreakBeforeBraces: Linux`)
- Pointer alignment right (`int *p`)
- `snake_case` for functions, `lower_case` for variables
- No typedef structs
- Space before parens on control statements
- `InsertBraces: true` — no omitted braces
- Sort includes with regroup: local headers → C types → stdlib → system

## P0/P1 Implementation Design

### P0: coffee check

1. Parse `Coffee.toml`
2. Validate manifest (package name, version required; warn on missing optional fields)
3. Collect files: from `[lib]` sources/headers, plus `src/*.c`, `include/**/*.h`
4. Build include flags: `-Iinclude`, `-Ideps/<dep>/include`, plus any from `[lib]`
5. Run `clang -fsyntax-only <flags> <files>`
6. Pass exit code

### P0: coffee build

1. Find `Coffee.toml` → project dir
2. Run bare `make -C <dir>` (default goal `all`)
3. Pass `RELEASE=1`, `DEBUG=1`, `-j N`, `CFLAGS_EXTRA=...` as make variables

### P1: coffee test / bench

- `coffee test` → `make test`
- `coffee test --test <name>` → `TEST_FILTER=<name> make test`
- `coffee bench` → `make bench`
- Makefile test target supports `TEST_FILTER` env variable for filtering

### P1: coffee add

- Change `pkg-version` from flag to string argument
- Format: with version → `name = "1.0"`, with features → `name = { version = "1.0", features = ["feat1"] }`
- `--optional` → `optional = true`
- `--dev` → `[dev-dependencies]` table, `--build` → `[build-dependencies]` table
- Append Makefile flags as before

### P1: coffee remove

- Remove manifest entry (existing)
- Scan Makefile for `# Dep: <name>` section and remove it

### P1: coffee update — semver

- Parse `^1.0`, `>=2.0`, `=1.2.3`, `*` constraints
- Extend `registry_get_versions()` to return all versions from registry
- Match constraint against available versions, pick latest
- Update `Coffee.lock`

### P1: coffee tree — transitive

- For each direct dep, fetch recipe via `registry_get()`
- Parse recipe's `dependencies` field
- Recurse, display with tree-drawing characters

### P1: coffee new --lib

- `--lib` → `src/lib.c` + `include/<name>/<name>.h`, no `main()`
- Makefile builds `build/lib<name>.a` (static library)
- `--bin` (default): current behavior unchanged
