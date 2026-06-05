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
├── TODO.md              # ~78% complete, targeting v0.4 "Publishable"
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
│   ├── strings.h        # Safe printf/snprintf wrappers
│   ├── cargo_clone.c    # Standalone helper: Cargo-compatible CLI wrapper
│   └── commands/        # 42 subcommand implementations, one .c per command
│       ├── add.c, build.c, check.c, clean.c, config.c, ...
│       ├── run.c, test.c, doc.c, search.c, info.c, ...
│       ├── fetch.c, update.c, tree.c, vendor.c, ...
│       └── generate_lockfile.c, install.c, init.c, new.c, ...
│
├── include/             # Vendored third-party dependencies (maintained in-tree)
│   ├── sds/             # antirez/sds — Simple Dynamic Strings
│   ├── toml.c, toml.h   # cktan/tomlc99 — TOML parser
│   └── safe.h           # Arena allocator / safe memory wrappers
│
├── tests/
│   ├── test_framework.h/.c   # Custom lightweight test framework (macros + runner)
│   ├── test_main.c           # Test entry point: registers suites, runs filters
│   ├── test_features.c       # Feature resolution tests
│   ├── test_makefile.c       # Makefile generation tests
│   ├── test_cflags_libs.c    # Compiler/linker flags tests
│   ├── test_manifest_version.c  # Version extraction tests
│   ├── test_stubs.c          # Stub command tests
│   ├── test_registry_*.c     # Registry fetch/version tests
│   └── test_framework_test.c # Meta-tests for the framework itself
│
├── docs/                # mdBook documentation (42 command reference pages)
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

| Type | File | Purpose |
|------|------|---------|
| `options_s` (`options`) | `src/coffee.h` | Flattened CLI state — threaded through every command handler. Holds all flags, paths, feature strings, build mode. |
| `command_s` | `src/coffee.h` | Dispatch entry: `.name`, `.description`, `.action(options*) → i64`. |
| `manifest_t` | `src/manifest.h` | Parsed `Coffee.toml`. Contains `package_t`, `dependencies_t`, `feature_def_t[]`, `test_section_t`. |
| `package_t` | `src/manifest.h` | Package metadata: name, version, edition, description, license, sources, headers. Contains raw `dependencies[]` (flat strings). |
| `dependency_t` | `src/manifest.h` | A single structured dependency: name, version, path, git, branch, tag, rev, optional. Populated from inline tables. |
| `dependencies_t` | `src/manifest.h` | Array of `dependency_t`. |
| `test_section_t` | `src/manifest.h` | Test configuration: sources, harness, framework. |
| `feature_def_t` | `src/manifest.h` | Named feature: name + array of dependency feature strings. |
| `build_opts_t` | `src/build.h` | Build parameters: verbose, release/debug, target, features, jobs. |
| `resolved_features_t` | `src/coffee_features.h` | Resolved feature sets per package, with per-package `feature_set_t`. |
| `feature_set_t` | `src/coffee_features.h` | A set of feature names (sds array). |
| `lockfile_t` | `src/lockfile.h` | Parsed `Coffee.lock`. Contains `lockfile_dep_t[]` entries (name, path, version, commit). |
| `lockfile_dep_t` | `src/lockfile.h` | A single locked dependency: name, path, version, commit SHA. |
| `recipe_t` | `src/registry.h` | Registry recipe: name, version, license, download_url, dependencies. |
| `recipe_list_t` | `src/registry.h` | Array of `recipe_t` (search results). |
| `version_list_t` | `src/registry.h` | Array of version strings for a package. |

**Relationships:**
- `command_s` → `options_s` → command handler → `manifest_t` (via `project_find_manifest` + `manifest_parse`)
- Handler may also use `lockfile_t` (via `lockfile_parse`), `recipe_t`/`recipe_list_t` (via registry), or `build_opts_t` → `build_project()` / `compile_sources()`
- Dep resolution helpers (`dep_resolve_dir`, `dep_add_flags`, `dep_parse_name`) in `build.h` are shared across commands
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

**Build flow** (`src/commands/build.c` → `src/build.c`):
```
handle_build(&opt) → build_project(manifest, &opts)
    → features_resolve() → resolved_features_t
    → features_to_compiler_flags() → -DFEATURE_* flags
    → glob("src/*.c") for source files
    → resolve dependencies (lockfile + filesystem search via dep_resolve_dir)
    → collect dep source files via dep_add_flags
    → fork+exec $CC (default: clang) with flags + sources + dep sources
    → output to target/debug/<name> or --target-dir
```

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

**Doc flow** (`src/commands/doc.c`):
```
handle_doc(&opt)
    → Dispatch: "doc check" → doc_check() (verify doxygen installed)
    → Dispatch: "doc generate" or bare "doc" → doc_generate()
      → Find and parse manifest
      → If Doxyfile exists, run doxygen
      → If [doc] section exists without Doxyfile, generate Doxyfile then run doxygen
      → Otherwise print tip
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
build_project() / compile_sources():
    manifest_t + build_opts_t + compiled flags + dep sources
    → fork+exec $CC -DFEATURE_* src/*.c deps/*/src/*.c -o target/debug/<name>
    ↓
Binary output

Coffee.lock (TOML)
    → lockfile_parse() → lockfile_t
    → build uses lockfile to resolve dep paths and pinned commits
    ↓
Reproducible builds with pinned git SHAs
```

## Design decisions and rationale

- **No build system generation.** Coffee compiles directly via `fork`/`exec` of `clang`/`$CC`; it does not generate Makefiles or CMake files. This keeps the build path simple and avoids intermediate files. See `src/build.c`.

- **No external registry.** The project is designed for git and path-based dependency management. Registry commands (`publish`, `yank`, `owner`) are not implemented. The old `registry.c` client still exists as a fallback for `name = "version"` style deps but emits warnings.

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

## External dependencies

| Dependency | Source | Used in | Purpose |
|------------|--------|---------|---------|
| **sds** | `include/sds/` (antirez/sds) | Everywhere | Dynamic string type (replaces `char *`) |
| **toml** | `include/` (cktan/tomlc99) | `src/manifest.c` | Parse `Coffee.toml` into `manifest_t` |
| **safe** | `include/` | Various | Arena allocator / safe memory wrappers |
| **curl** | system (`pkg-config --libs`) | `src/registry.c` | HTTP downloads (called via `system()`) |
| **zlib** | system (`-lz`) | `src/registry.c` | Decompress registry index |
| **git** | system | `src/commands/fetch.c`, `generate_lockfile.c` | Clone/fetch git deps, resolve commit SHAs |
| **getopt_long** | libc (POSIX) | `src/cmdline.c` | CLI argument parsing via getopt_long |
| **clang** | system | `src/build.c`, `src/commands/test.c` | Default compiler (fork+exec `$CC`) |
| **clang-format** | build-time tool | `fmt` command | Code formatting |
| **clang-tidy** | build-time tool | `lint`/`fix` commands | Static analysis / auto-fix |
| **doxygen** | build-time tool | `src/commands/doc.c` | API documentation generation |

## Entry points

| Mode | Entry point | Notes |
|------|------------|-------|
| **CLI tool** | `src/coffee.c:main()` | Normal build; compiles to `bin/coffee` |
| **Test runner** | `tests/test_main.c:main()` | Built with `-DCOFFEE_TEST_RUNNER` (suppresses coffee's main). Links all test objects + src objects excluding `coffee.o`. |
| **`coffee test` generated runner** | `compile_sources()` in `src/commands/test.c` | Dynamically compiled test binary from project + test sources. |
| **Individual commands** | `src/commands/<name>.c:handle_<name>()` | Each command is a standalone function called via the `commands[]` dispatch table. |
