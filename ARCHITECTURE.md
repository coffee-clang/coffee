# Architecture

This file documents the high-level architecture of the codebase.
It helps AI agents understand the project structure, key modules,
data flow, and design decisions without having to re-discover
them on every exploration.

## Directory layout

```
.
├── Coffee.toml          # Project manifest (uses itself as a test case)
├── Makefile             # Build system: static binary via clang -std=c23
├── .clang-format        # Formatting: tabs (width 4), 120-column limit
├── .clang-tidy          # Linting: aggressive C-only checks, warnings-as-errors
├── DESIGN.md            # Design philosophy and rationale
├── TODO.md              # ~94% complete, targeting v1.0
├── ARCHITECTURE.md      # This file
│
├── src/
│   ├── coffee.c         # Entry point: CLI dispatch via commands[] table
│   ├── coffee.h         # Central types: options_s, command_s, type aliases
│   ├── cmdline.c/.h     # Hand-written CLI parser (getopt_long)
│   ├── manifest.c/.h    # Coffee.toml parser/writer (TOML → manifest_t)
│   ├── project.c/.h     # Manifest discovery (walk up dirs for Coffee.toml)
│   ├── build.c/.h       # Build orchestration: fork+exec $CC, feature flags, dep resolution
│   ├── registry.c/.h    # Remote registry client (curl-based, secondary path)
│   ├── coffee_features.c/.h  # Feature resolution: features → -DFLAGS
│   ├── lockfile.c/.h    # Coffee.lock parser/writer (dependency pinning)
│   ├── dep_graph.c/.h   # Transitive dependency graph resolver (DFS + cycle detection)
│   ├── strings.h        # Safe printf/snprintf wrappers
│   ├── compat_limits.h  # Polyfill for C23 stdckdint on older toolchains
│   ├── cargo_clone.c    # Standalone helper: Cargo-compatible CLI wrapper
34|d6343a69 │   └── commands/        # 43 command entries (40 handlers + 3 aliases), one .c per command
│       ├── add.c, build.c, check.c, clean.c, config.c, ...
│       ├── run.c, test.c, doc.c, search.c, info.c, ...
│       ├── fetch.c, update.c, tree.c, vendor.c, ...
│       └── generate_lockfile.c, install.c, init.c, new.c, ...
│
├── include/             # Vendored third-party headers (maintained in-tree)
│   ├── sds/             # antirez/sds — Simple Dynamic Strings headers
│   ├── toml.h           # cktan/tomlc99 — TOML parser header
│   └── safe.h           # Safe string wrappers + bump-pointer arena allocator
│
│   (sds.c and toml.c are compiled as part of src/ for convenience)
│
├── tests/
│   ├── test_framework.h/.c   # Custom lightweight test framework (macros + runner)
│   ├── test_main.c           # Test entry point: registers suites, runs filters
│   ├── test_features.c       # Feature resolution tests
│   ├── test_dep_graph.c      # Dependency graph resolution + cache tests (31 tests)
│   ├── test_lockfile.c       # Lockfile parsing/writing tests
│   ├── test_config.c         # Config get/set/unset tests
│   ├── test_doc.c            # Doc generation tests
│   ├── test_version.c        # Version comparison tests
│   ├── test_install.c        # Install command tests
│   ├── test_makefile.c       # Makefile generation tests
│   ├── test_cflags_libs.c    # Compiler/linker flags tests
│   ├── test_manifest_version.c  # Version extraction tests
│   ├── test_manifest_bin.c   # Binary target tests
│   ├── test_framework_test.c # Meta-tests for the framework itself
│   ├── test_coverage_*.c     # Coverage tests for commands, build, cmdline, core, install, manifest
│   └── test_commands_*.c     # Command integration tests (basic, deps, manifest)
│
65|68b369c4 ├── docs/                # mdBook documentation (43 command reference pages)
│   ├── index.md         # Introduction / quick start
│   ├── features.md      # Feature system guide (~400 lines)
│   ├── SUMMARY.md       # Table of contents
│   ├── theme/           # CSS overrides
│   └── commands/        # One .md per command
│
├── bin/                 # Output directory (static binary, test runner)
├── scripts/             # CI/utility scripts
└── .github/             # CI workflows
```

## Key types and their relationships

All defined in `src/coffee.h` and module headers.

| Type                    | File                    | Purpose                                                                                                                         |
| ----------------------- | ----------------------- | ------------------------------------------------------------------------------------------------------------------------------- | ------------- | ------------------------------------------------------------------------------------------------------------------------- |
| `options_s` (`options`) | `src/coffee.h`          | Flattened CLI state — threaded through every command handler. Holds all flags, paths, feature strings, build mode.              |
| `command_s`             | `src/coffee.h`          | Dispatch entry: `.name`, `.description`, `.action(options*) → i64`.                                                             |
| `manifest_t`            | `src/manifest.h`        | Parsed `Coffee.toml`. Contains `package_t`, `dependencies_t`, `feature_def_t[]`, `test_section_t`.                              |
| `package_t`             | `src/manifest.h`        | Package metadata: name, version, edition, description, license, sources, headers. Contains raw `dependencies[]` (flat strings). |
| `dependency_t`          | `src/manifest.h`        | A single structured dependency: name, version, path, git, branch, tag, rev, optional. Populated from inline tables.             |
| `dependencies_t`        | `src/manifest.h`        | Array of `dependency_t`.                                                                                                        |
| `test_section_t`        | `src/manifest.h`        | Test configuration: sources, harness, framework.                                                                                |
| `feature_def_t`         | `src/manifest.h`        | Named feature: name + array of dependency feature strings.                                                                      |
| 91                      | c3f83182                | `build_opts_t`                                                                                                                  | `src/build.h` | Build parameters: verbose, release, debug, locked, target, target_dir, jobs, features, all_features, no_default_features. |
| `resolved_features_t`   | `src/coffee_features.h` | Resolved feature sets per package, with per-package `feature_set_t`.                                                            |
| `feature_set_t`         | `src/coffee_features.h` | A set of feature names (sds array).                                                                                             |
| `lockfile_t`            | `src/lockfile.h`        | Parsed `Coffee.lock`. Contains `lockfile_dep_t[]` entries (name, path, version, commit).                                        |
| `lockfile_dep_t`        | `src/lockfile.h`        | A single locked dependency: name, path, version, commit SHA.                                                                    |
| `dep_node_t`            | `src/dep_graph.h`       | A node in the dependency graph: name, path, version, commit, flags, sources, git metadata.                                      |
| `dep_graph_t`           | `src/dep_graph.h`       | Transitive dependency graph: array of `dep_node_t`, root at index 0. Built via `dep_graph_create()`.                            |
| `recipe_t`              | `src/registry.h`        | Registry recipe: name, version, license, download_url, dependencies.                                                            |
| `recipe_list_t`         | `src/registry.h`        | Array of `recipe_t` (search results).                                                                                           |
| `version_list_t`        | `src/registry.h`        | Array of version strings for a package.                                                                                         |

**Relationships:**

104|d83b8057 - `command_s` → `options_s` → command handler → `manifest_t` (via `project_find_manifest` + `manifest_parse`).
105|90e3e389 For `build`/`run`/`compile`: handler checks for Makefile, then runs `make`.

- Handler may also use `lockfile_t` (via `lockfile_parse`), `recipe_t`/`recipe_list_t` (via registry), `build_opts_t` → `build_project()` / `compile_sources()`, or `dep_graph_t` (via `dep_graph_create()`)
- `dep_graph_create(manifest, lockfile, offline)` resolves all transitive dependencies, producing a flat graph with the root package at index 0. It handles git, path, and registry deps recursively
- `manifest_t` stores deps in two parallel forms: `package.dependencies[]` (raw flat strings) and `dependencies.deps[]` (structured `dependency_t` from inline tables)

## Control flow

```
CLI invocation
    ↓
src/coffee.c: main()  (not compiled when COFFEE_TEST_RUNNER is defined)
    ↓
cmdline_parser() [getopt_long] → cli_args
    ↓
Populate options_s struct (borrows strings from args_info)
    ↓
Match first non-toolchain input against commands[] table
    ↓
commands[i].action(&opt)  →  command handler in src/commands/<name>.c
```

**Typical command handler pattern** (seen in ~35+ commands):

```
handle_xxx(options *opt):
    manifest_path = project_find_manifest(nullptr)
    manifest = manifest_parse(manifest_path)
    // ... work with manifest, lockfile, or build ...
    manifest_free(manifest)
    return 0
```

136|9f44e403 **Build flow** (`src/commands/build.c`):

139|ddcbe9c3 handle*build(&opt)
140|30b3ac21 → Find Coffee.toml and resolve features (features_resolve → -DFEATURE*_ flags)
141|eff7f8cd → Check for Makefile; if missing, error out with "Coffee requires a Makefile"
142|5eb0eb46 → If Makefile found: fork+exec make -C <project*dir> [RELEASE=1] [DEBUG=1] [-j<N>]
143|fe4fdff8 with CFLAGS_EXTRA="-DFEATURE*_ ..."
144|20153950 → build_project() (direct fork+exec $CC) exists but is dead code in this command;
145|fa16e113 it is only reachable from `coffee test` via compile_sources()

**Test flow** (`src/commands/test.c`):

```
handle_test(&opt)
    → Find and parse Coffee.toml
    → Determine test sources: from [test] section or glob tests/*.c
    → Determine project sources: glob src/*.c
    → Resolve dependency flags (lockfile + dep_resolve_dir + dep_add_flags)
    → compile_sources() → fork+exec $CC -DCOFFEE_TEST_RUNNER → test binary
    → Run test binary with optional filter (inputs[1] or TEST_FILTER env)
    → Return exit code
```

**Fetch flow** (`src/commands/fetch.c`):

```
handle_fetch(&opt)
    → Parse manifest dependencies
    → For each dep:
      → If inline table with git → git clone/fetch into ~/.coffee/deps/<name>,
        symlink to deps/<name>, record commit SHA in lockfile
      → If inline table with path → resolve absolute path, symlink to deps/<name>
      → If flat string ("name = version") → fall back to registry_fetch()
```

**Registry flow** (`src/commands/search.c`, `install.c`, `metadata.c`, etc.):

```
registry_search() / registry_get()
    → curl (via system()) → ~/.coffee/packages.json (cached index)
    → parse JSON lines → recipe_t / recipe_list_t
```

This is a secondary, deprecated path. The project is designed for git/path-based
dependency management without a central registry.

**Dep graph flow** (`src/dep_graph.c`):

```
dep_graph_create(manifest, lockfile, offline)
    → Add root package as node[0] from manifest
    → For each direct dependency:
      → Resolve path: lockfile → dep_resolve_dir() → registry
      → If git dep: git rev-parse HEAD → pinned commit
      → Add dep node with flags (-I, -L, -l) and source files
      → Recurse: parse dep's library.toml or Coffee.toml for transitive deps
      → DFS with visited set for cycle detection (warns, does not error)
    → Return dep_graph_t with flat array of all transitive deps

dep_graph_get(manifest, lockfile, offline, project_dir)
    → Check .coffee/build-cache/<package>.graph for cached dep_graph_t
    → Validate cache: compare stored mtimes of Coffee.toml/Coffee.lock against current
    → On cache hit: deserialize and return
    → On cache miss: call dep_graph_create(), serialize to cache, return
    → When project_dir is null: fall back to uncached dep_graph_create()
```

**Doc flow** (`src/commands/doc.c`):

```
handle_doc(&opt)
    → Dispatch: "doc check" → doc_check() (verify doxygen installed)
    → Dispatch: "doc generate" or bare "doc" → doc_generate()
      → Find and parse manifest
      → If Doxyfile exists, run doxygen
      → If [doc] section exists without Doxyfile, read settings (project-name, output-dir,
        input-dirs, exclude-patterns) and generate Doxyfile, then run doxygen
      → Otherwise print tip
```

**Config flow** (`src/commands/config.c`):

```
handle_config(&opt)
    → No args / "list" / "--list" → config_list() — prints all key=value pairs from ~/.coffee/config.toml
    → "get <key>" → config_read_raw(section, key) — supports dotted keys (section.key)
    → "set <key> <value>" → config_set_raw(section, key, value) — updates file in-place
    → "unset <key>" → config_unset_raw(section, key) — removes the line from file
```

**Lockfile generation** (`src/commands/generate_lockfile.c`):

```
handle_generate_lockfile(&opt)
    → Parse manifest dependencies
    → For each dep, resolve directory via dep_resolve_dir()
    → For git deps, record resolved commit SHA via git rev-parse HEAD
    → Write Coffee.lock with pinned version and commit per dep
```

## Data flow

```
Coffee.toml (TOML)
    → manifest_parse() [toml.c] → manifest_t
    → features_resolve() → resolved_features_t
    → features_to_compiler_flags() → sds *flags[]
    ↓
247|85daea0f build (Makefile path, primary):
248|cb5cdf69     make -C <project_dir> with CFLAGS_EXTRA="-DFEATURE_* ..." and RELEASE/DEBUG flags
249|9a379a13     ↓
250|ecae33b3 Binary output
251|00000000
252|85daea0f test (direct path): compile_sources():
253|7ba28ecc     manifest_t + build_opts_t + compiled flags
254|215fe9d1     → dep_graph_create(manifest, lockfile, offline) → dep_graph_t
255|33e42bea     → dep_graph_flags() + dep_graph_sources() for each transitive dep
256|c3a842ad     → fork+exec $CC -DCOFFEE_TEST_RUNNER -DFEATURE_* src/*.c deps/*/src/*.c -o test binary
257|9a379a13     ↓
258|d06b6f45 Test binary output

Coffee.lock (TOML)
    → lockfile_parse() → lockfile_t
    → build uses lockfile to resolve dep paths and pinned commits
    ↓
Reproducible builds with pinned git SHAs
```

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

## Design decisions and rationale

264|54330a73 - **Makefile-based build.** `coffee build`, `run`, and `compile` require a Makefile and invoke `make`.
265|76fde475 `coffee init` and `coffee new` generate a Makefile template. `coffee test` bypasses this
266|b6ff7338 and compiles directly via `compile_sources()` (fork+exec `$CC`). The direct-build path
267|4cad99ec (`build_project()` in `src/build.c`) exists but is dead code in the `build` command — it is
268|5e6d84c4 only reachable from `test`. See `src/build.c` and `src/commands/build.c`.

- **External registry.** The list of available packages can be downloaded at
  https://coffee-clang.github.io/recipes/.well-known/packages.json.zstd. This registry is read-only, therefore there
  is no command for managing the registry, adding packages, and so on.
- **Git deps are first-class.** Dependencies declared as `name = { git = "...", ... }` are cloned into `~/.coffee/deps/<name>` and symlinked into `deps/<name>`. The lockfile records the pinned commit SHA for reproducible builds.

- **Path deps for workspaces.** Dependencies declared as `name = { path = "./path" }` are symlinked into `deps/<name>`. This enables multi-project workspaces without any network or central server.

- **Vendored dependencies.** Dependencies (`sds`, `toml`, `safe`) live in `include/` and are downloaded via `make bootstrap` from GitHub. No package manager bootstrapping problem. See `Makefile` target `bootstrap`.

- **Hand-written CLI parser.** `cmdline.c`/`cmdline.h` implement argument parsing directly using `getopt_long`. This avoids the gengetopt dependency while keeping parsing simple and C-compatible.

- **SDS strings throughout.** The project uses `sds` (Simple Dynamic Strings) instead of raw `char *` for all mutable string data. This prevents buffer overflows and simplifies concatenation. `src/strings.h` provides wrappers around banned `fprintf`/`sprintf` families.

- **Feature resolution is custom.** Rather than importing a SAT solver, Coffee has a lightweight, iterative feature-resolution engine in `src/coffee_features.c` that detects circular dependencies and maps feature names to `-DFEATURE_<UPPERCASE>` compiler flags.

- **C23 with strict linting.** Code uses `nullptr`, `[[nodiscard]]`, `static_assert`, `<stdckdint.h>`. `.clang-tidy` enforces `bugprone-*`, `cert-*`, `clang-analyzer-*` as errors. `NOLINT` is banned in source files.

- **No public API.** Coffee is a CLI tool, not a library. All headers are internal to `src/` and `include/`.

- **Test framework is bespoke.** Rather than depending on a test library, `tests/test_framework.h` provides a macro-based registration system (`TEST()`, `TEST_REGISTER()`, `ASSERT()`). Tests are compiled into the same binary via `-DCOFFEE_TEST_RUNNER` (which suppresses `main()` in `coffee.c`).

- **`coffee test` is self-contained.** Compiles project sources + test sources into a standalone binary using `compile_sources()`, without needing a Makefile. Uses lockfile or filesystem to resolve dependency include paths and flags.

- **Doc subcommands.** `coffee doc` accepts subcommands: `generate` (create Doxyfile + run doxygen) and `check` (verify doxygen is installed). Subcommand dispatch via `opts->inputs[1]`.

- **Manifest stores deps twice.** The flat `package.dependencies[]` array preserves the raw TOML strings (backwards compatibility with existing commands like `tree`, `update`, `metadata`). The structured `dependencies.deps[]` array is populated from inline tables (`name = { git = "...", ... }`) and used by `fetch`, `generate_lockfile`, and `build`.

- **Transitive dependency graph.** `dep_graph.c` resolves the full transitive dependency tree via DFS, producing a flat array with the root at index 0. Each node carries resolved paths, compiler flags, source files, and git metadata. This replaces the older ad-hoc `dep_resolve_dir()` / `dep_add_flags()` approach with a single, cacheable data structure shared across `build`, `test`, `tree`, and `outdated` commands.

- **Arena allocator in safe.h.** `include/safe.h` provides a bump-pointer arena (`struct arena`) with block chaining. Allocations from an arena are freed together via `arena_reset()` or `arena_destroy()`. Individual `arena_alloc()` pointers cannot be freed separately. Default block size is 64 KiB. This is used internally by `dep_graph` and other modules that need bulk temporary allocations.

- **Dep graph caching.** `dep_graph_get()` caches resolved dependency graphs as TOML files in `.coffee/build-cache/<package>.graph`. Cache invalidation compares the mtimes of `Coffee.toml` and `Coffee.lock` against stored timestamps, plus a `cache_version` field for format evolution. Used by `build`, `metadata`, and `generate-lockfile` to avoid repeated DFS resolution.

## External dependencies

| Dependency       | Source                       | Used in                                                      | Purpose                                   |
| ---------------- | ---------------------------- | ------------------------------------------------------------ | ----------------------------------------- |
| **sds**          | `include/sds/` (antirez/sds) | Everywhere                                                   | Dynamic string type (replaces `char *`)   |
| **toml**         | `include/` (cktan/tomlc99)   | `src/manifest.c`                                             | Parse `Coffee.toml` into `manifest_t`     |
| **safe**         | `include/`                   | Various                                                      | Arena allocator / safe memory wrappers    |
| **curl**         | system (`pkg-config --libs`) | `src/registry.c`                                             | HTTP downloads (called via `system()`)    |
| **zlib**         | system (`-lz`)               | `src/registry.c`                                             | Decompress registry index                 |
| **git**          | system                       | `src/commands/fetch.c`, `dep_graph.c`, `generate_lockfile.c` | Clone/fetch git deps, resolve commit SHAs |
| **getopt_long**  | libc (POSIX)                 | `src/cmdline.c`                                              | CLI argument parsing via getopt_long      |
| **clang**        | system                       | `src/build.c`, `src/commands/test.c`                         | Default compiler (fork+exec `$CC`)        |
| **clang-format** | build-time tool              | `fmt` command                                                | Code formatting                           |
| **clang-tidy**   | build-time tool              | `lint`/`fix` commands                                        | Static analysis / auto-fix                |
| **doxygen**      | build-time tool              | `src/commands/doc.c`                                         | API documentation generation              |

## Entry points

| Mode                               | Entry point                                  | Notes                                                                                                                    |
| ---------------------------------- | -------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| **CLI tool**                       | `src/coffee.c:main()`                        | Normal build; compiles to `bin/coffee`                                                                                   |
| **Test runner**                    | `tests/test_main.c:main()`                   | Built with `-DCOFFEE_TEST_RUNNER` (suppresses coffee's main). Links all test objects + src objects excluding `coffee.o`. |
| **`coffee test` generated runner** | `compile_sources()` in `src/commands/test.c` | Dynamically compiled test binary from project + test sources.                                                            |
| **Individual commands**            | `src/commands/<name>.c:handle_<name>()`      | Each command is a standalone function called via the `commands[]` dispatch table.                                        |
