# Coffee Project TODO

> Overall completeness: **~94%** — targeting v1.0

## Priority Legend

| Priority | Meaning                    | Target audience       |
| -------- | -------------------------- | --------------------- |
| P0       | Must-have for "it works"   | Everyone              |
| P1       | Major usability gap        | Newbies & daily users |
| P2       | Important but non-blocking | Intermediate users    |
| P3       | Nice-to-have               | Power users           |

---

## P0 — Must-Have

### `coffee check` should use project flags

✅ **DONE** — Parses Coffee.toml, passes include paths from manifest (`-Isrc`, `-Iinclude`, `-Iinclude/<name>`), checks all `.c`/`.h` files.

### `coffee build` incremental awareness

✅ **DONE** — Generates a `Makefile` in the project root from the manifest, the resolved dependency graph
and the project sources (`src/makefile.c`), writing it only when its content changes, then runs `make` on
the requested target (default `all`; `coffee build test` runs the `test` target). The generated build is
self-contained and static (sources + `-I` + `-DFEATURE_*` baked in). `[compilation] Makefile = "legacy"`
runs the existing Makefile as-is with `RELEASE=1`/`DEBUG=1`/`CFLAGS_EXTRA` passed through. `coffee run`
shares the same `build_project()` routine. Supports `--release`, `--debug`, `-j`, and feature flags.

### Transitive dependency resolution

✅ **DONE** — New `dep_graph` module (`src/dep_graph.h/.c`) resolves the full transitive dependency graph via DFS. Reads each dep's `Coffee.toml` or `library.toml` recursively. Used by fetch, build, generate-lockfile, outdated, update, and metadata commands.

### Git ref checkout

✅ **DONE** — `coffee fetch` now checks out the configured `branch`/`tag`/`rev` from `Coffee.toml` instead of always using `origin/HEAD`.

### Registry dependencies installed from recipes

✅ **DONE** — A dependency declared as a plain version (`argparse = "1.1.0"`) is installed by running its
recipe's `install.sh` (`src/recipe_install.c`), which fetches the source (git or archive), builds it and
installs it into `~/.coffee/<name>/<version>/`; `deps/<name>` links there, so `cflags`, `libs` and the
generated Makefile pick up its `include/` and `lib/`. The recipe version must satisfy the version in
`Coffee.toml`. A recipe's own `[dependencies]` are installed first and become graph nodes. The script's
output goes to `~/.coffee/recipes/<name>/install.log` (last lines shown on failure, `--verbose` streams
it). A stamp holding the sha256 of `install.sh` makes later fetches reuse the install until the script
changes. Recipes without `install.sh` still clone `recipe_url`.

### `coffee build` / `run` / `test` fetch missing dependencies

✅ **DONE** — A declared dependency (direct or transitive) that is not on disk is fetched before
building, so a fresh checkout builds in one step. With `--offline` it is an error instead.

### `--locked` / `--frozen`

✅ **DONE** — Both flags were parsed but ignored. `--locked` now requires `Coffee.lock` and fails a fetch
that would change it; a registry dep whose recipe version or `install.sh` checksum differs from the lock
is refused before its script runs. `--frozen` is `--locked` plus `--offline`. `Coffee.lock` records the
recipe version and `checksum` (sha256 of `install.sh`) for registry deps.

---

## P1 — Major Usability Gaps

### `coffee test` — proper compilation, not shell loop

✅ **DONE** — Compiles project + test sources directly via `compile_sources()` (fork+exec `$CC -DCOFFEE_TEST_RUNNER`) into a standalone test binary, without needing a Makefile. Supports `TEST_FILTER` env var / input arg to run a single test. Test runner prints pass/fail per test and exits nonzero on failure.

### `coffee bench` — same treatment as test

✅ **DONE** — Delegates to `make bench` with `INC_FLAGS` derived from Coffee.toml include paths. Requires a Makefile with a `bench` target; compilation and timing are handled by that target.

### `coffee add` — support version, features, optional

✅ **DONE** — Supports `--version` (via `opts->pkg_version`), `--features`, `--optional`, `--dev`, `--build`. Appends dependency flags to Makefile. A bare package name is looked up in the registry and recorded with the recipe version (`name = "<version>"`), which `fetch` installs from the recipe; `--git` / `--path` add git and path deps.

### `coffee remove` — clean up Makefile

✅ **DONE** — When removing a dependency, its `# Dep: <name>` section is removed from the Makefile.

### `coffee update` — git-aware resolution

✅ **DONE** — Uses `dep_graph` to resolve all transitive deps. For git deps, runs `git fetch` + checkout ref, records new commit SHA in lockfile. Registry deps are reinstalled from the current recipe (the new version must satisfy `Coffee.toml`). Supports selective update of a single dep.

### `coffee outdated` — git-aware comparison

✅ **DONE** — Compares pinned commit SHA against remote tracking ref for git deps. Reports commits behind. Registry deps are compared with the registry's current recipe version. Other non-git deps shown as informational.

### `coffee tree` — show transitive dependencies

✅ **DONE** — Fetches and displays the full dependency tree recursively (up to depth 3). Uses filesystem resolution (via `dep_resolve_dir`) rather than registry.

### `coffee new` — support `--lib` / `--bin`

✅ **DONE** — `--lib` generates `src/lib.c` + `include/<name>/<name>.h` + library Makefile, no `main()`. `--bin` (default) keeps current behavior with `src/main.c`.

### `coffee generate-lockfile` — transitive deps

✅ **DONE** — Now records all transitive dependencies (from dep_graph) in `Coffee.lock`, including git commit SHAs.

### Remove registry dependency

✅ **DONE** — `search` queries the remote registry index; `info` reads project metadata from `Coffee.toml`. `vendor` uses git clone. `install` uses `--git`. `metadata` and `report audit` use `dep_graph` instead of registry lookups. The registry is used where it is the source of a dependency: `add <name>`, and `fetch` / `update` / `outdated` for plain-version deps.

---

## P2 — Important But Non-Blocking

### `coffee doc` — manifest-driven doc generation

✅ **DONE** — `doc generate` reads `[doc]` section from `Coffee.toml` (project-name, input-dirs, exclude-patterns), generates a Doxyfile, then runs doxygen. Output always goes to `doc/` (HTML in `doc/html/`). Falls back to pre-existing Doxyfile if no `[doc]` section. `doc check` verifies doxygen is installed.

### `coffee config` — subcommand support

✅ **DONE** — Supports `--list` (print all config), `get <key>`, `set <key> <value>`, `unset <key>`. Stores config in `~/.coffee/config.toml`. Supports dotted keys (`section.key`). Default (no args) prints all config.

### `coffee metadata` — add resolved features + transitive deps

✅ **DONE** — Outputs JSON with package name, version, edition, description, license, dependencies, resolved feature set, and full transitive dependency tree (via dep_graph, no registry).

### `[compilation]` — configure the generated build

⬜ **OPEN** — The generated Makefile hardcodes `-static`, `clang` and `-O3 -g`, none of which
`Coffee.toml` can change. Add optional `cflags` / `ldflags` arrays and a `static` bool to the
`[compilation]` section so a project that cannot be linked statically, or needs another standard,
is buildable by coffee. Deferred on purpose: it is a `Coffee.toml` schema change and deserves its
own plan; today the documented escape hatch is `[compilation] Makefile = "legacy"`.

### `coffee build --bin` / `--example` / `--profile` / `--target`

⬜ **OPEN** — The parser accepts these, and `coffee build` now rejects them with
"not yet implemented" instead of silently building the default target. `--bin` needs
multiple targets with per-target object rules and a different link recipe; `--target` needs
cross-compilation support.

---

## P3 — Nice-to-Have

### `coffee fix` — smarter auto-fix

✅ **DONE** — Runs `clang-tidy --fix` on source files using project-specific include paths from Coffee.toml (instead of hardcoded `-Isrc`).

### `coffee lint` — per-file linting

✅ **DONE** — Runs `clang-tidy` on source files with project-specific include paths. Supports `--fix` flag to apply fixes inline.

### `coffee install` — compile binaries from git

✅ **DONE** — Accepts `--git <url>` to clone and compile binaries from git repos. Installs to `~/.coffee/bin/`. No registry fallback.

### `coffee version` — show dependency versions

✅ **DONE** — Prints coffee version from `CMDLINE_PARSER_VERSION`.

### `coffee help` — per-command help

✅ **DONE** — `coffee help` lists all available commands with descriptions.

### Transitive dependency caching / offline build

✅ **DONE** — `dep_graph_get()` caches resolved dependency graphs in `.coffee/build-cache/<package>.graph`. Cache is invalidated when `Coffee.toml` or `Coffee.lock` mtimes change, or when the cache format version changes.

- [x] Cache resolved dep paths in `.coffee/build-cache/`
- [x] Only re-resolve when `Coffee.toml` or `Coffee.lock` changes
- [x] Used by `build`, `metadata`, `generate-lockfile` commands

### Quiet tool checks

✅ **DONE** — The startup tool checks ran `clang/git/make/curl/zstd --version` with only stderr silenced,
so every `build`, `fetch` and `outdated` printed version banners. They now discard stdout as well
(`RUN_CMD_NOOUT`).

### Branch coverage ≥ 95%

✅ **DONE** — `make coverage` builds a gcc `--coverage` runner and fails when the project's branch coverage
(vendored sds/toml/include excluded) is below `COVERAGE_THRESHOLD` (default 95). It went from 86.4% to 95.8%
(4006/4180 branches; lines 98.3%). Most of the arms still uncovered cannot be reached through the API:
`== nullptr` checks after allocators that abort, fork/pipe/dup2/fclose failures, the branches of a forked
child that `_exit()`s or execs, and null slots or names that the parsers never produce.

---

## Completion Targets by Milestone

| Milestone                      | Target % | Key deliverables                                                 |
| ------------------------------ | -------- | ---------------------------------------------------------------- |
| **v0.2** — "Just runs"         | 55%      | P0 items: script-mode run, check uses flags, build incremental   |
| **v0.3** — "Testable"          | 65%      | P1: test/bench improvements, semver update, tree with transitive |
| **v0.4** — "Publishable"       | 78%      | P2: metadata features+deps, fix/lint --fix, bench Makefile       |
| **v0.5** — "Dep management"    | 85%      | Transitive deps, git ref checkout, git-aware outdated/update     |
| **v1.0** — "Coffee-competitive" | 94%+     | All P0–P3 done. 771 tests.                                       |

---

## Known Issues

| Issue                          | Severity | Detail                                                           |
| ------------------------------ | -------- | ---------------------------------------------------------------- |
| `cov_new_lib_mode` flaky       | Low      | Not reproduced in 13 consecutive full-suite runs (2026-11-08), after the test runner disabled git auto-gc. Reopen if it fails again. |
| Recipes only unpack sources    | Medium   | Most recipes' `install.sh` only extract the source into `~/.coffee/<name>/<version>/` without building, so there is no `include/` or `lib/` for `cflags`/`libs`. To be fixed in the recipes repository. |
| Bare versions are exact        | Low      | `name = "1.1.0"` (what `coffee add` writes) is an exact match, so `update` cannot move past it when the registry bumps the recipe; `outdated` still reports it. Use `">= 1.1"` to allow updates. |
| Recipe scripts are trusted     | Info     | `fetch` runs `install.sh` from the registry as the user. Needs `bash`, `sha256sum`, `tail`, plus whatever the scripts call (`wget`, `git`, `cmake`, ...). |
| `fetch` path dep inside `deps/<name>` | High | `self = { path = "./deps/self/sub" }` with a real `deps/self/` directory: `fetch_path_dep` runs `rm -rf deps/self`, deleting the source it just resolved, then links to the missing path. |
| `config set` top-level key     | Medium   | `in_target_section` is only set for sectioned keys, so setting a top-level key twice writes a duplicate line (invalid TOML), and after a `[section]` header it lands in that section. |
| `config unset` ignores section | Medium   | `config_unset_raw` ignores its section: `config unset a.x` removes `x` from every section and the top level. |
| `build_project` without cwd    | Low      | When `getcwd()` fails (deleted cwd, path > 4096) the chdir into the project is skipped, so the legacy `Makefile` check and `Coffee.lock` are looked up in the wrong directory while `make -C` uses the right one. |
| `manifest_write` null feature dep | Low   | The comma is written before the null check, so a null first entry writes `f = [, "x"]` (invalid TOML). |
| `features_parse_cli` null outputs | Low   | The guard returns on null outputs but the body still writes `*out_features`/`*out_count`. No caller passes null. |
| Empty dependency name          | Low      | A `"=1.0"` dependency entry yields an empty name: `libs` prints a bare `-l`, `check` adds `-I"deps/"`. |
| Compact JSON registry index    | Info     | `extract_string_val` only matches keys at the start of a line, so a single-line JSON index yields entries without fields. |
| `doc_generate` test            | Low      | Emits "doxygen not installed" error when doxygen is absent; test handles gracefully but user experience could be smoother. |

---

## How to Contribute

1. Pick any item and create a dedicated branch
2. Read the command's `docs/commands/<name>.md`
3. Implement the handler in `src/commands/<name>.c`
4. Add tests in `tests/test_<name>.c`
5. Run `make tidy` to ensure lint-clean
6. Open a PR

See `README.md` for build instructions and coding standards.
