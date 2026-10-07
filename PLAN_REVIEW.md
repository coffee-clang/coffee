# Review Fix Plan — Generated-Makefile Build

Status: **implemented — all 8 tasks done, suite green (436 tests), tidy clean, ASan verified**

Implementation notes (deviations from the plan, all confirmed against actual behavior):

- **Task 6 test spec:** the plan listed `build/hello.o` as an accepted target, but its own §2.3
  charset `[A-Za-z0-9_.-]` rejects `/` (as did the pre-existing inline loop). The test asserts
  `build/hello.o` is rejected; `all`, `test`, `hello.o` accepted.
- **Task 4 allowlist:** `*` and `?` were added to the `[A-Za-z0-9._/+@=-]` allowlist — required for
  source globs (`src/*.c`), which the plan also required to keep working.
- **Task 4 test:** `render_falls_back_on_unsafe_target_dir` asserts the injected string
  `build;rm -rf /` is absent, not `rm -rf` — the generated `clean` target legitimately contains
  `rm -rf $(TARGET_DIR)`.
- **Task 1 verification:** `make sanitize-address TEST_FILTER=build_all_flags_legacy` passes; the
  first run tripped the `make test` Coffee.toml guard on leftover dirt from a direct (guard-bypassing)
  suite run, not on a real modification.
- **Task 3 smoke test:** a real prebuilt-only path dep (`deps/mylib` with `lib/libmylib.a`) now
  links via `LDLIBS := -lmylib` expanded after `$(SOURCES)`; the binary builds and runs.
- **Task 5 smoke test:** default build uses clang; `CC=gcc coffee build` compiles with gcc.

Scope is unchanged: harden `build_project` / `makefile_*` and close the test gaps.
Every finding below was re-verified against the tree before being kept, corrected, or
added.

---

## 0. What changed in this revision

| # | Original plan | Correction |
| - | ------------- | ---------- |
| A | Task 4: change `CC := clang` to `CC ?= clang` | **Wrong.** `CC` has a *built-in default* (`$(origin CC)` = `default`, value `cc`), and `?=` only assigns when the origin is `undefined`. `CC ?= clang` silently switches the compiler from clang to `cc`. Needs an `ifeq ($(origin CC),default)` guard. See §2.1. |
| B | Task 3: `build_project` returns the exe path, `run.c` drops its manifest parse | **Incomplete.** Nothing said what happens in legacy mode, where the binary location is not owned by `coffee`. Dropping the parse breaks the existing `run_with_manifest` test. Both modes need the path. See §2.2. |
| C | Task 6: target validation test asserts `..` is rejected | **`..` is currently accepted** (the charset allows `.`; only a leading `-` is refused). The test would fail. See §2.3. |
| D | Task 4: temp file named `.Makefile.tmp.<pid>` | Deviates from all three sibling writers (`manifest.c:667`, `lockfile.c:131`, `dep_graph_cache.c:20`), which all use `<path>.tmp`. See §2.4. |
| E | Task 6: "generated-target coverage" placed in `tests/test_makefile.c` | That file never invokes `make` (no `system()` anywhere in it). Coverage that needs `make` belongs in `tests/test_commands_deps.c`. See §2.5. |
| F | *(missing)* | **New finding:** `-l` is emitted into `CFLAGS`, which the link rule expands *before* `$(SOURCES)` → prebuilt-only deps cannot link. New Task 3. |
| G | *(missing)* | **New finding:** the injection surface is wider than `library.toml` `include`/`lib`: every dep flag token, plus `m->package.sources[]` / `m->bin[].src[]` into `SOURCES`. Folded into Task 4. |
| H | Acceptance criterion "423 existing + new tests" | Unverified count. Replaced with "existing suite green + N new tests". |

Arithmetic nit: Task 1 asked for `char *make_argv[10]` justifying "8 + `nullptr` = 10".
The worst case is 9 slots; 10 is safe, but the comment must not carry the wrong arithmetic.

---

## 1. Findings (re-verified)

### Blocking

- **`src/build.c` — `build_project`, `char *make_argv[8]` (decl. ~line 764).** Confirmed
  out-of-bounds write of one pointer. Legacy mode with `--release --debug -jN
  --features f <target>` fills slots 0–7 (`make`, `-C`, dir, `-jN`, `RELEASE=1`,
  `DEBUG=1`, `CFLAGS_EXTRA=…`, target), then writes `nullptr` at index 8. Reachable only
  when `rf != nullptr` **and** `features_to_compiler_flags` returns ≥ 1 flag — otherwise
  the `CFLAGS_EXTRA` slot is skipped and the overflow does not happen. Any test must
  guarantee that flag count.

### Should fix

- **`src/makefile.c:70` — `CC := clang`** overrides `CC=gcc coffee build` / `make CC=…`.
  The fix is *not* `?=` (see §2.1).
- **`src/makefile.c` — link order for prebuilt-only deps (NEW).** For a dep with
  `src_count == 0`, both `-L` and `-l` tokens are emitted as `CFLAGS += …`, and the link
  rule is `$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDFLAGS)`. `clang -static -Lx -lx -o app
  src/main.c` puts the archive on the link line *before* the objects that need it, so ld's
  one-pass resolution fails with undefined symbols. The `-l` must move after `$(SOURCES)`.
- **`src/makefile.c` — untrusted values interpolated verbatim (WIDENED).** The original
  review named `library.toml` `include`/`lib`. In fact every dep flag token
  (`-I`/`-L`/`-l`) passes through, plus `m->package.sources[]` / `m->bin[].src[]` into
  `SOURCES` (a `$(…)`-bearing entry is expanded at parse time, even inside
  `$(wildcard …)`), plus `edition` (`-std=…`) and `opts->target_dir` (`TARGET_DIR := …`).
  `package.name` is the one interpolated value already validated, by `dep_name_is_valid`
  inside `dep_graph_create` (the root-node guard in `src/dep_graph.c`) — the only
  production path to `makefile_render`.
- **`src/commands/run.c` — binary path is cwd-relative.** `build_project` builds under
  `project_dir` and restores the cwd before returning; `run.c` then looks for
  `build/<name>` relative to the cwd. `coffee run` from a subdirectory fails.
- **`src/makefile.c` — write path is not durable and is not uniform.** No
  `fflush`/`fsync` before `safe_fclose`, unlike the three sibling writers.
- **`src/makefile.c` — dep with *both* sources and a prebuilt `lib/`.** `-L`/`-l` are
  dropped ("sources win", per the single-static-binary decision). Intentional; document
  it and cover the prebuilt-only case.

### Nits

- `src/commands/build.c:20-30` — the inline target charset loop belongs in `build.c`
  next to the other validators.
- `src/commands/add.c:15` — `is_safe_package_name` is a byte-for-byte third copy of
  `dep_name_is_valid`. **Out of scope**; do not touch, note as follow-up.
- `src/makefile.c:52` — `is_glob` treats `[` as a wildcard.
- `src/makefile.c:20-33` — `src_list_add` dedup is O(n²).
- `src/makefile.c:66` — `makefile_render` is exported but validates only non-null; state
  the precondition in its header comment.
- `tests/test_makefile.c:322,340` — two `sleep(1)` add ~2 s to the suite.
- No test covers the `-j`/`RELEASE`/`DEBUG`/`CFLAGS_EXTRA` argv path, the generated
  `clean`/`test` targets, or target-name validation.

---

## 2. Corrections in detail

### 2.1 `CC ?= clang` does not work

GNU make predefines `CC = cc` with origin `default`. `?=` is defined as
`ifeq ($(origin FOO),undefined)`, and a built-in default is *not* `undefined`, so the
assignment never fires and the project would build with `cc`. The project needs clang for
`-std=c23`. The correct form — honour env / command-line `CC`, but beat make's own
default — is the origin guard:

```
ifeq ($(origin CC),default)
CC := clang
endif
```

This preserves current behavior for everyone who has not asked for a compiler, and makes
`CC=gcc coffee build` and `make CC=…` work as the review intended.

### 2.2 Legacy mode and the exe path

`build_project` must fill the out-parameter in **both** modes as
`<project_dir>/<opts->target_dir or "build">/<package name>`:

- generated mode: exactly where the rendered `TARGET` points;
- legacy mode: the Makefile chooses where the binary goes, but `<project_dir>` is still
  strictly more correct than the current cwd-relative path, and it is what keeps the
  existing `run_with_manifest` fixture (a legacy Makefile producing `build/run`) green.

If `run.c` relied on the out-parameter alone and `build_project` left it unset in legacy
mode, `run_with_manifest` regresses. Both modes must be covered by the new tests.

### 2.3 Target validation accepts `..`

The current rule is: non-empty, does not start with `-`, charset `[A-Za-z0-9_.-]`.
`..` satisfies it. `a/b`, `a b`, `-x` and `""` are rejected. The new test must assert the
actually-rejected set; do not add `..` rejection in this change (`../Makefile` is already
rejected by the `/` ban, so there is no reachable hazard).

### 2.4 Temp-file naming

`manifest.c:667`, `lockfile.c:131` and `dep_graph_cache.c:20` all write `<path>.tmp`.
A PID suffix only in `makefile.c` makes the tree inconsistent and leaves the same race in
the other three writers. Keep `Makefile.tmp` here; the shared fix is a separate change
(§6).

### 2.5 Which test file

`tests/test_makefile.c` calls `makefile_render`/`makefile_generate` in-process and never
executes `make`. `tests/test_commands_deps.c` already drives `handle_build`/`handle_run`
against real fixtures (`build_generates_makefile`, `build_target_argument`,
`run_with_manifest`); anything needing `make` or a handler belongs there. Also note that
`setup_project()` in that file *always* appends `[compilation] Makefile = "legacy"` — a
generated-mode test must rewrite `Coffee.toml` and `remove("Makefile")` first, exactly as
`build_generates_makefile` does.

---

## 3. Fix plan

### Task 1: Fix the `make_argv` stack overflow

**Files:**
- Modify: `src/build.c` (`build_project`, ~line 764)
- Test: `tests/test_commands_deps.c`

**Purpose:** Remove the out-of-bounds write when every legacy flag is combined.

**Scope:** Size the array for the worst case (8 arguments + `nullptr` = 9 slots; use 10 to
leave one slot of slack) and replace the bare declaration with a comment enumerating the
slots in push order: `make`, `-C`, dir, `-jN`, `RELEASE=1`, `DEBUG=1`, `CFLAGS_EXTRA=…`,
target, `nullptr`. Keep it a fixed array — no allocation churn for a fixed upper bound.
Test `build_all_flags_legacy`: `setup_project("build-allflags", …)` (legacy mode), a
Makefile whose `test` target touches a sentinel, `Coffee.toml` extended with
`[features]\nextra = []`, then `handle_build` with `inputs = {"build","test"}`, `jobs = 2`,
`release = 1`, `debug = 1`, `features = "extra"`; assert exit 0 and the sentinel exists.
The fixture must make `features_to_compiler_flags` return ≥ 1 flag — otherwise
`CFLAGS_EXTRA` is omitted, `make_argc` stays at 7, and the test passes even against the
unfixed code.

**Expected Result:** `make sanitize-address TEST_FILTER=build_all_flags_legacy` fails
before the fix (stack-buffer-overflow), passes after; `make test` green; `make tidy` clean.

**Alternative:** growable argv list — rejected, allocation churn for a fixed bound.

---

### Task 2: Return the executable path from `build_project`

**Files:**
- Modify: `src/build.c`, `src/build.h` — `i64 build_project(options *opts, const char *target, sds *exe_path_out)`
- Modify: `src/commands/build.c` (pass `nullptr`)
- Modify: `src/commands/run.c` (use the returned path; delete the manifest find/parse block)
- Test: `tests/test_commands_deps.c`

**Purpose:** `coffee run` must resolve the binary against the project directory in both
generated and legacy mode, and stop re-parsing a manifest `build_project` already parsed.

**Scope:** Once `project_dir` and `manifest` are known (before the make invocation, in
either branch), compute `<project_dir>/<target_dir|build>/<package name>` and store it in
`*exe_path_out` when non-null; the caller frees with `sdsfree`. `handle_run` calls
`build_project(opts, "all", &exe_path)` and execs the result, keeping the existing `X_OK`
check and argument passthrough. Tests: `run_from_subdirectory_generated` (rewrite
`Coffee.toml` without the legacy opt-in, `remove("Makefile")`, `chdir("src")`, expect 0)
and `run_from_subdirectory_legacy` (keep the legacy fixture, `chdir("src")`, expect 0).
Both must fail on current code; `run_no_manifest` and `run_with_manifest` must stay green.

**Expected Result:** Both new tests fail before (`build/<name>` looked up from `src/`),
pass after; existing run tests unchanged.

**Alternative:** shared `load_manifest(options*)` helper — reduces but does not remove the
duplicated lookup and leaves the cwd bug.

---

### Task 3: Put `-l` after the objects on the link line (NEW)

**Files:**
- Modify: `src/makefile.c` (dep flag loop ~line 96–104; link rule ~line 176)
- Test: `tests/test_makefile.c`

**Purpose:** A dependency that ships no sources but a prebuilt library must link.

**Scope:** In the dep-flag loop, keep `-L` (and `-I`) in `CFLAGS`, but route `-l` into a
new accumulated `LDLIBS` variable emitted after the sources:
`$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDLIBS) $(LDFLAGS)`. Emit `LDLIBS := …` even when
empty so the rendered file stays stable. Test `render_prebuilt_dep_link_flags` (hand-built
graph, `src_count = 0`, flags `-I… -L… -l…`; assert `-lmylib` is emitted, appears in the
`LDLIBS` line, and that the recipe ends `$(SOURCES) $(LDLIBS) $(LDFLAGS)`); the negative
case is already covered by `render_dep_flags_and_sources` (a dep with sources must emit
neither `-L` nor `-l`). `render_basic`'s recipe assertion must be updated for `$(LDLIBS)`.

**Expected Result:** New test passes; existing render tests stay green.

**Alternative:** keep `-l` in `CFLAGS` and wrap the link in `-Wl,--start-group` — rejected,
obscures the recipe. Leaving as-is is rejected because it is a silent link failure.

---

### Task 4: Validate every value interpolated into the Makefile

**Files:**
- Modify: `src/makefile.c` (dep flag loop; `edition` ~line 76; `target_dir` ~line 145;
  `SOURCES` loop; `makefile.h` precondition comment)
- Test: `tests/test_makefile.c`

**Purpose:** No third-party `library.toml` value or manifest field may inject a make or
shell construct into a file that is parsed on every `coffee build`, including
`coffee build clean`.

**Scope:** Add a file-local `static bool mk_value_is_safe(const char *s)` with an
allowlist of `[A-Za-z0-9._/+@=-]` — enough for real include paths, `-I`, `-L`, `-l`,
`-isystem`, `-DFEATURE_X`, and relative source globs. Apply it to every dep flag token,
to every `SOURCES` entry before it is emitted (so a `$(…)` in a dep source path is dropped
before it reaches `$(wildcard …)`), to `edition`, and to `target_dir`. Failure policy,
matching the existing deny-then-warn posture: a dep-derived token or source is **skipped
with a stderr warning naming the dep and the value**; an unsafe `edition` or `target_dir`
falls back to `c23` / `build` with a warning. Never fail the build over a cosmetic value —
`coffee build clean` must keep working. `package.name` is already validated upstream; keep
`makefile_render`'s non-null guard and document the precondition in `makefile.h` rather
than re-validating. Tests: `render_skips_unsafe_dep_flag` (dep flags containing
`$(shell touch /tmp/pwned)` absent from the output), `render_skips_unsafe_dep_source`,
`render_falls_back_on_unsafe_edition` (`edition = "c23\n\t$(shell id)"` → `-std=c23`),
`render_falls_back_on_unsafe_target_dir`, and `render_accepts_normal_values` as the
regression guard for `render_dep_flags_and_sources` (`-Ideps/mylib/include` must survive).

**Expected Result:** New tests pass; existing render tests unchanged.

**Alternative:** escaping with `$$`/backslash or quoting — rejected, quoting does not stop
make expansion and the rules differ between make and shell; an allowlist is auditable.

---

### Task 5: Durable Makefile write + correct compiler default

**Files:**
- Modify: `src/makefile.c` (temp path ~line 258; write path ~line 282; `CC` line ~line 70)
- Test: `tests/test_makefile.c` (`render_basic` assertion at line 204)

**Purpose:** A crash or ENOSPC must not leave a truncated Makefile, and an explicit `CC`
must be honored while the default stays clang.

**Scope:** Keep the temp name `<project_dir>/Makefile.tmp`, matching `manifest.c:667`,
`lockfile.c:131` and `dep_graph_cache.c:20`. Insert `fflush(fp)` and `fsync(fileno(fp))`
before `safe_fclose`, each followed by `safe_fclose` + `remove(tmp_path)` on failure —
mirror `dep_graph_cache.c:93-113`, keeping the existing `safe_rename` + `safe_fsync_dir`.
Replace `CC := clang` with the `ifeq ($(origin CC),default)` guard from §2.1. Test
`render_honors_cc_override`: the output contains the origin guard and `CC := clang` inside
it, and contains no `CC ?=`; update `render_basic`'s `CC := clang` assertion to match.

**Expected Result:** Render tests green; manual `CC=gcc coffee build` in a smoke fixture
compiles with gcc, and the default build still uses clang.

**Alternative:** `mkstemp` — equivalent; the PID/`mkstemp` variant is deferred to §6.

---

### Task 6: Nits

**Files:**
- Modify: `src/makefile.c` (`is_glob` ~line 52: drop `[`; comment on the O(n²) dedup in `src_list_add` ~line 20)
- Modify: `src/build.c`, `src/build.h` (`bool build_target_is_valid(const char *target)` — the current non-empty / no-leading-`-` / `[A-Za-z0-9_.-]` rule, kept *distinct* from `dep_name_is_valid`, which bans `.` and would break `hello.o`)
- Modify: `src/commands/build.c` (use it)
- Test: `tests/test_commands_deps.c`

**Purpose:** Remove the duplicated charset rule and the `[`-as-wildcard surprise.

**Scope:** As listed. Do **not** touch `src/commands/add.c`'s duplicate
`is_safe_package_name`. The target-validation test asserts `""`, `-x`, `a b`, `a/b` are
rejected (exit 1) and `all`, `test`, `hello.o`, `build/hello.o` accepted — `..` is accepted
today and stays accepted (§2.3).

**Expected Result:** Target tests green; no behavior change for valid targets.

---

### Task 7: Test-quality fixes

**Files:**
- Modify: `tests/test_makefile.c` (`generate_creates_and_preserves`, ~line 322 and 340)
- Modify: `tests/test_commands_deps.c`

**Purpose:** Deterministic no-rewrite proof, and the missing generated-target coverage.

**Scope:** Replace both `sleep(1)` calls with `utimensat(AT_FDCWD, path, times, 0)`
backdating the generated `Makefile` to a fixed old timestamp after the first generate;
assert the mtime equals the backdated value after the no-op generate and differs after the
content-changing generate (`<sys/stat.h>`; `_GNU_SOURCE` is already set by the Makefile).
Add `build_generated_clean_target` and `build_generated_test_target` in
`tests/test_commands_deps.c` on a generated-mode fixture (rewrite `Coffee.toml` to drop the
legacy opt-in, `remove("Makefile")`): `clean` removes `build/`, `test` runs the stub
target. Add `render_prebuilt_dep_link_flags` per Task 3.

**Expected Result:** Suite is ~2 s faster and green; existing suite + new tests all pass.

---

### Task 8: Docs sync

**Files:**
- Modify: `docs/commands/build.md` (default compiler is clang but `CC` in the environment or on the make command line wins; deps with sources are compiled in, prebuilt-only deps link via `-L`/`-l`; interpolated values are validated and unsafe ones skipped with a warning; the file is written atomically)
- Modify: `docs/commands/run.md` (the binary is resolved relative to the project directory, not the current directory — it currently claims `build/<name>`)
- Modify: `ARCHITECTURE.md` (line 168 flow and line 407 rationale: `build_project` also returns the resolved executable path)

**Purpose:** Keep docs truthful after Tasks 2–5.

**Expected Result:** No stale claims; `make docs` if mdbook is available.

---

## 4. Order & dependencies

1. **T1** `build.c` argv — no deps.
2. **T2** exe path — after T1 (same function).
3. **T3** link order — `makefile.c` render, no deps.
4. **T4** value hardening — after T3 (same dep-flag loop).
5. **T5** write path + `CC` — after T4 (same file).
6. **T6** nits — after T5 (same file).
7. **T7** tests — after all code tasks (assertions encode final behavior).
8. **T8** docs — last.

## 5. Risks

- **T4 allowlist vs. real paths.** Dep paths containing spaces or unusual characters are
  already unrepresentable in the current unquoted renderer, so skipping them is not a new
  class of breakage — but the warning must name the dependency and the value, or users
  cannot diagnose a missing `-I`.
- **T4 may affect the project's own build.** The root `Coffee.toml` uses itself as a test
  case; `make tidy` + `make test` after T4 is the gate.
- **T2 changes a signature.** Two call sites (`commands/build.c`, `commands/run.c`), both
  updated in the same change.
- **T3 is the one observable behavior change** (prebuilt-only deps now link in the right
  order). Nothing in-tree depends on the broken order.
- **T1 verification** needs the local `make sanitize-address` pass;
  `make sanitize-address TEST_FILTER=build_all_flags_legacy` isolates it.

## 6. Explicitly out of scope (follow-ups)

- `src/commands/add.c:15` `is_safe_package_name` — third copy of `dep_name_is_valid`.
- The shared `<path>.tmp` collision under concurrent `coffee build` in one project —
  affects `manifest.c`, `lockfile.c`, `dep_graph_cache.c` and `makefile.c`; fixing one
  site alone would be inconsistent, so it needs its own plan.
- The generated `test`/`bench` targets are `@echo` stubs; wiring `test` to `coffee test`
  is a design decision, not a fix.