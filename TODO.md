# Coffee Project TODO

> Generated from `cargo`-equivalent analysis. Overall completeness: **~78%**

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

✅ **DONE** — Runs `make` without arguments when a Makefile is present. Falls back to `build_project()` for projects without Makefile. Supports `--release`, `--debug`, `-j`, and feature flags.

---

## P1 — Major Usability Gaps

### `coffee test` — proper compilation, not shell loop

✅ **DONE** — Uses `make bin/tests/runner` infrastructure instead of a raw shell loop. Supports `TEST_FILTER` env var / input arg to run a single test. Test runner prints pass/fail per test and exits nonzero on failure.

### `coffee bench` — same treatment as test

✅ **DONE** — Now compiles `bench/*.c` files into `bin/bench/` via Makefile and runs each with `time`. Passes include paths and compiler flags from Coffee.toml via `INC_FLAGS`.

### `coffee add` — support version, features, optional

✅ **DONE** — Supports `--version` (via `opts->pkg_version`), `--features`, `--optional`, `--dev`, `--build`. Appends dependency flags to Makefile.

### `coffee remove` — clean up Makefile

✅ **DONE** — When removing a dependency, its `# Dep: <name>` section is removed from the Makefile.

### `coffee update` — semver-aware resolution

✅ **DONE** — Parses version constraints (`^1.0`, `>=2.0`, `=1.2.3`, `*`), uses registry version list to find matching version, writes `Coffee.lock`.

### `coffee tree` — show transitive dependencies

✅ **DONE** — Fetches and displays the full dependency tree recursively (up to depth 3). Uses registry to look up transitive deps.

### `coffee new` — support `--lib` / `--bin`

✅ **DONE** — `--lib` generates `src/lib.c` + `include/<name>/<name>.h` + library Makefile, no `main()`. `--bin` (default) keeps current behavior with `src/main.c`.

---

## P2 — Important But Non-Blocking

### `coffee doc` — manifest-driven doc generation

❌ **NOT IMPLEMENTED** — Currently just checks for a pre-existing `Doxyfile` and runs `doxygen`. No manifest-driven config generation.

- [ ] Generate a default Doxyfile or read doc config from `Coffee.toml` `[doc]` section

### `coffee config` — subcommand support

❌ **NOT IMPLEMENTED** — Currently prints environment variables only. No `--list`, `get <key>`, `set <key> <value>`.

- [ ] Implement `coffee config --list`
- [ ] Implement `coffee config get <key>`
- [ ] Implement `coffee config set <key> <value>`
- [ ] Store config in `~/.coffee/config.toml`

### `coffee metadata` — add resolved features + transitive deps

✅ **DONE** — Outputs JSON with package name, version, edition, description, license, dependencies, resolved feature set, and full transitive dependency tree (up to depth 3 via registry).

---

## P3 — Nice-to-Have

### `coffee fix` — smarter auto-fix

✅ **DONE** — Runs `clang-tidy --fix` on source files using project-specific include paths from Coffee.toml (instead of hardcoded `-Isrc`).

### `coffee lint` — per-file linting

✅ **DONE** — Runs `clang-tidy` on source files with project-specific include paths. Supports `--fix` flag to apply fixes inline.

### `coffee install` — compile binaries

⚠️ **PARTIAL** — Fetches packages from registry and creates symlinks in `.coffee/deps/`. Does **not** detect `[[bin]]` tables or compile binaries to `~/.coffee/bin/`.

- [ ] Detect `[[bin]]` table in `library.toml`
- [ ] Compile and install binaries to `~/.coffee/bin/`

### `coffee version` — show dependency versions

✅ **DONE** — Prints coffee version from `CMDLINE_PARSER_VERSION`.

### `coffee help` — per-command help

✅ **DONE** — `coffee help` lists all available commands with descriptions.

---

## Completion Targets by Milestone

| Milestone                      | Target % | Key deliverables                                                 |
| ------------------------------ | -------- | ---------------------------------------------------------------- |
| **v0.2** — "Just runs"         | 55%      | P0 items: script-mode run, check uses flags, build incremental   |
| **v0.3** — "Testable"          | 65%      | P1: test/bench improvements, semver update, tree with transitive |
| **v0.4** — "Publishable"       | 78%      | P2: metadata features+deps, fix/lint --fix, bench Makefile       |
| **v0.5** — "Feature complete"  | 85%      | P2: publish, yank, owner, config subcommands, doc generation     |
| **v1.0** — "Cargo-competitive" | 90%+     | P3 polish: install [[bin]], ecosystem, edge cases                |

---

## How to Contribute

1. Pick any item and create a dedicated branch
2. Read the command's `docs/commands/<name>.md`
3. Implement the handler in `src/commands/<name>.c`
4. Add tests in `tests/test_<name>.c`
5. Run `make check` to ensure lint-clean
6. Open a PR

See `README.md` for build instructions and coding standards.
