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
│   ├── build.c/.h       # Build orchestration: fork+exec $CC, feature flags
│   ├── registry.c/.h    # Remote registry client: curl-based search/fetch
│   ├── coffee_features.c/.h  # Feature resolution: features → -DFLAGS
│   ├── strings.h        # Safe printf/snprintf wrappers
│   ├── cargo_clone.c    # Standalone helper: Cargo-compatible CLI wrapper
│   └── commands/        # 43 subcommand implementations, one .c per command
│       ├── add.c, build.c, check.c, clean.c, config.c, ...
│       ├── run.c, test.c, bench.c, search.c, info.c, ...
│       └── publish.c, yank.c, owner.c  # (stubs — not yet implemented)
│
├── deps/                # Vendored third-party dependencies (no package mgr)
│   ├── sds/             # antirez/sds — Simple Dynamic Strings
│   ├── toml/            # cktan/tomlc99 — TOML parser (C99)
│   └── safe/            # Arena allocator / safe memory wrappers
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
├── docs/                # mdBook documentation (43 command reference pages)
│   ├── index.md         # Introduction / quick start
│   ├── features.md      # Feature system guide (~400 lines)
│   ├── SUMMARY.md       # Table of contents
│   ├── theme/           # CSS overrides
│   └── commands/        # One .md per command (many marked "Stub")
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
| `manifest_t` | `src/manifest.h` | Parsed `Coffee.toml`. Contains `package_t`, `dependencies_t`, `feature_def_t[]`. |
| `package_t` | `src/manifest.h` | Package metadata: name, version, edition, description, license, sources, headers. |
| `dependency_t` | `src/manifest.h` | A single dependency: name, version constraint, optional path/git/branch/tag/rev. |
| `dependencies_t` | `src/manifest.h` | Array of `dependency_t`. |
| `feature_def_t` | `src/manifest.h` | Named feature: name + array of dependency feature strings. |
| `build_opts_t` | `src/build.h` | Build parameters: verbose, release/debug, target, features, jobs. |
| `resolved_features_t` | `src/coffee_features.h` | Resolved feature sets per package, with per-package `feature_set_t`. |
| `feature_set_t` | `src/coffee_features.h` | A set of feature names (sds array). |
| `recipe_t` | `src/registry.h` | Registry recipe: name, version, license, download_url, dependencies. |
| `recipe_list_t` | `src/registry.h` | Array of `recipe_t` (search results). |
| `version_list_t` | `src/registry.h` | Array of version strings for a package. |

**Relationships:** `command_s` → `options_s` → command handler → `manifest_t` (via `project_find_manifest` + `manifest_parse`) → optionally `recipe_t`/`recipe_list_t` (via registry) or `build_opts_t` → `build_project()`.

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

**Typical command handler pattern** (seen in ~30+ commands):
```
handle_xxx(options *opt):
    manifest_path = project_find_manifest(nullptr)
    manifest = manifest_parse(manifest_path)
    // ... work with manifest, registry, or build ...
    manifest_free(manifest)
    return 0
```

**Build flow** (`src/commands/build.c` → `src/build.c`):
```
handle_build(&opt) → build_project(manifest, &opts)
    → features_resolve() → resolved_features_t
    → features_to_compiler_flags() → -DFEATURE_* flags
    → glob("src/*.c") for source files
    → fork+exec $CC (default: clang) with flags + sources
    → output to target/debug/<name> or --target-dir
```

**Registry flow** (`src/commands/search.c`, `install.c`, etc.):
```
registry_search() / registry_get()
    → curl (via system()) → ~/.coffee/packages.json (cached index)
    → parse JSON lines → recipe_t / recipe_list_t
```

**Test flow:**
```
make test  →  compile tests/test_*.c + src/*.c (with -DCOFFEE_TEST_RUNNER)
    → bin/tests/runner [--verbose] [filter]
    → test_framework_run(filter) walks global registry, runs matches
    → test_framework_summary() → "X passed, Y failed, Z total"
```

## Data flow

```
Coffee.toml (TOML)
    → manifest_parse() [toml.c] → manifest_t
    → features_resolve() → resolved_features_t
    → features_to_compiler_flags() → sds *flags[]
    ↓
build_project():
    manifest_t + build_opts_t + compiled flags
    → fork+exec $CC -DFEATURE_* src/*.c -o target/debug/<name>
    ↓
Binary output
```

## Design decisions and rationale

- **No build system generation.** Coffee compiles directly via `fork`/`exec` of `clang`/`$CC`; it does not generate Makefiles or CMake files. This keeps the build path simple and avoids intermediate files. See `src/build.c`.

- **Vendored dependencies.** Dependencies (`sds`, `toml`, `safe`) are downloaded via `make bootstrap` from GitHub and checked in. No package manager bootstrapping problem. See `Makefile` target `bootstrap`.

- **Hand-written CLI parser.** `cmdline.c`/`cmdline.h` implement argument parsing directly using `getopt_long`. This avoids the gengetopt dependency while keeping parsing simple and C-compatible.

- **SDS strings throughout.** The project uses `sds` (Simple Dynamic Strings) instead of raw `char *` for all mutable string data. This prevents buffer overflows and simplifies concatenation. `src/strings.h` provides wrappers around banned `fprintf`/`sprintf` families.

- **Feature resolution is custom.** Rather than importing a SAT solver, Coffee has a lightweight, iterative feature-resolution engine in `src/coffee_features.c` that detects circular dependencies and maps feature names to `-DFEATURE_<UPPERCASE>` compiler flags.

- **Registry over HTTP.** The registry is a static GitHub Pages site with JSON index files. Coffee uses `curl` (via `system()`) to download the index; parsing is done with naive line-by-line JSON extraction (no JSON library).

- **C23 with strict linting.** Code uses `nullptr`, `[[nodiscard]]`, `static_assert`, `<stdckdint.h>`. `.clang-tidy` enforces `bugprone-*`, `cert-*`, `clang-analyzer-*` as errors. `NOLINT` is banned in source files.

- **No public API.** Coffee is a CLI tool, not a library. There is no `include/` directory — all headers are internal to `src/`.

- **Test framework is bespoke.** Rather than depending on a test library, `tests/test_framework.h` provides a macro-based registration system (`TEST()`, `TEST_REGISTER()`, `ASSERT()`). Tests are compiled into the same binary via `-DCOFFEE_TEST_RUNNER` (which suppresses `main()` in `coffee.c`).

## External dependencies

| Dependency | Source | Used in | Purpose |
|------------|--------|---------|---------|
| **sds** | `deps/sds/` (antirez/sds) | Everywhere | Dynamic string type (replaces `char *`) |
| **toml** | `deps/toml/` (cktan/tomlc99) | `src/manifest.c` | Parse `Coffee.toml` into `manifest_t` |
| **safe** | `deps/safe/` | Various | Arena allocator / safe memory wrappers |
| **curl** | system (`pkg-config --libs`) | `src/registry.c` | HTTP downloads (called via `system()`) |
| **zlib** | system (`-lz`) | `src/registry.c` | Decompress registry index |
| **getopt_long** | libc (POSIX) | `src/cmdline.c` | CLI argument parsing via getopt_long |
| **clang** | system | `src/build.c` | Default compiler (fork+exec `$CC`) |
| **clang-format** | build-time tool | `fmt` command | Code formatting |
| **clang-tidy** | build-time tool | `lint`/`fix` commands | Static analysis / auto-fix |

## Entry points

| Mode | Entry point | Notes |
|------|------------|-------|
| **CLI tool** | `src/coffee.c:main()` | Normal build; compiles to `bin/coffee` |
| **Test runner** | `tests/test_main.c:main()` | Built with `-DCOFFEE_TEST_RUNNER` (suppresses coffee's main). Links all test objects + src objects excluding `coffee.o`. |
| **Individual commands** | `src/commands/<name>.c:handle_<name>()` | Each command is a standalone function called via the `commands[]` dispatch table. |
