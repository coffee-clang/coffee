# Coffee.toml Specification

`Coffee.toml` is the project manifest file. It describes the package metadata, dependencies, features, binary targets, test configuration, and documentation settings. Every Coffee project has exactly one `Coffee.toml` at its root.

This document is the complete reference for the file format.

## Sections Overview

| Section | Type | Required | Description |
|---------|------|----------|-------------|
| `[package]` | Table | Yes | Package metadata (name, version, edition, etc.) |
| `[dependencies]` | Table | No | Project dependencies |
| `[features]` | Table | No | Conditional compilation features |
| `[[bin]]` | Array of tables | No | Binary targets |
| `[test]` | Table | No | Test configuration |
| `[doc]` | Table | No | Documentation generation settings |

---

## The `[package]` Section

The `[package]` section defines the package metadata.

```toml
[package]
name = "mylib"
version = "1.0.0"
edition = "c23"
description = "A sample library"
license = "MIT"
repository = "https://github.com/user/mylib"
authors = "Jane Doe"
```

### Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | String | Yes | Package name. Must be alphanumeric with `_` or `-`. |
| `version` | String | Yes | Package version (semver recommended). |
| `edition` | String | No | C standard edition (e.g., `"c23"`, `"c11"`). Default: `"c23"`. |
| `description` | String | No | Short description of the package. |
| `license` | String | No | SPDX license identifier (e.g., `"MIT"`, `"Apache-2.0"`). |
| `repository` | String | No | URL of the source repository. |
| `authors` | String | No | Author information. |

---

## The `[dependencies]` Section

Dependencies can be declared in two forms: **flat strings** (simple version constraints) or **inline tables** (detailed source specifications).

### Flat String Form

The simplest form specifies a name and version constraint:

```toml
[dependencies]
json-c = "1.0"
cjson = "2.1"
```

This form is used for registry-based dependencies. The version string supports semver constraints.

### Inline Table Form

For git or path dependencies, use inline tables:

```toml
[dependencies]
mylib = { git = "https://github.com/user/mylib.git" }
local-dep = { path = "../local-dep" }
pinned = { git = "https://github.com/user/pinned.git", tag = "v1.0.0" }
```

### Inline Table Fields

| Field | Type | Description |
|-------|------|-------------|
| `version` | String | Version constraint (for registry deps). |
| `git` | String | Git repository URL. |
| `path` | String | Local filesystem path (relative or absolute). |
| `branch` | String | Git branch to track. |
| `tag` | String | Git tag to pin. |
| `rev` | String | Specific git commit SHA. |
| `optional` | Boolean | If `true`, the dependency is only linked when a feature requires it. |
| `features` | Array of strings | Features to enable on this dependency. |
| `default-features` | Boolean | If `false`, disables the dependency's `default` feature. Default: `true`. |

### Examples

**Git dependency with branch:**

```toml
[dependencies]
mylib = { git = "https://github.com/user/mylib.git", branch = "develop" }
```

**Git dependency with tag:**

```toml
[dependencies]
stable-lib = { git = "https://github.com/user/stable.git", tag = "v2.0.0" }
```

**Git dependency with specific commit:**

```toml
[dependencies]
pinned = { git = "https://github.com/user/pinned.git", rev = "abc123def" }
```

**Path dependency:**

```toml
[dependencies]
local-utils = { path = "../utils" }
```

**Optional dependency (used with features):**

```toml
[dependencies]
openssl = { git = "https://github.com/openssl/openssl.git", optional = true }
```

**Dependency with features enabled:**

```toml
[dependencies]
mylib = { git = "https://github.com/user/mylib.git", features = ["json", "logging"] }
```

**Dependency with default features disabled:**

```toml
[dependencies]
mylib = { git = "https://github.com/user/mylib.git", default-features = false, features = ["xml"] }
```

---

## The `[features]` Section

Features enable conditional compilation and optional dependencies. Each feature is a name mapped to an array of dependency features or other feature names.

### Basic Feature

```toml
[features]
json = []
xml = []
logging = []
```

A feature with an empty array (`[]`) is a simple flag. When enabled, it passes `-DFEATURE_JSON` (uppercase, with `FEATURE_` prefix) to the compiler.

### Feature with Dependencies

```toml
[features]
tls = ["openssl"]
advanced-logging = ["log/max-level-debug", "regex"]
```

When the `tls` feature is enabled, the `openssl` dependency is linked. The `dep/feature` syntax enables a specific feature on a dependency.

### Default Features

```toml
[features]
default = ["json", "logging"]
json = []
logging = []
```

The `default` feature is enabled automatically unless `--no-default-features` is passed to `coffee build`.

### Optional Dependencies

Mark a dependency as `optional = true` in `[dependencies]`, then reference it in a feature:

```toml
[dependencies]
openssl = { git = "https://github.com/openssl/openssl.git", optional = true }

[features]
tls = ["openssl"]
```

If the `tls` feature is not enabled, `openssl` is not linked.

### Feature Resolution

When you build with features, Coffee:

1. Starts with explicitly requested features (from `--features` or package dependencies)
2. Adds default features unless `--no-default-features` is set
3. Walks the dependency graph, enabling required features on each dependency
4. Unifies features if the same dependency appears multiple times
5. Generates `-DFEATURE_<NAME>` for each feature in the resolved set

### Using Features in Code

```c
#ifdef FEATURE_JSON
    json_serialize(data);
#endif

#ifdef FEATURE_ADVANCED_LOGGING
    printf("[DEBUG] detailed log\n");
#endif
```

Feature names are converted to uppercase with `FEATURE_` prefix:
- `json` → `-DFEATURE_JSON`
- `advanced-logging` → `-DFEATURE_ADVANCED_LOGGING`

### Common Patterns

**Network vs CLI tool:**

```toml
[features]
network = ["curl"]
default = []

[dependencies]
curl = { git = "https://github.com/curl/curl.git", optional = true }
```

**Database backend selection:**

```toml
[features]
postgres = ["pq"]
mysql = ["mysqlclient"]
sqlite = []

[dependencies]
pq = { git = "https://github.com/lib/pq.git", optional = true }
mysqlclient = { git = "https://github.com/mysql/mysql.git", optional = true }
```

**Logging levels:**

```toml
[features]
log-error = []
log-warn = []
log-info = []
log-debug = []
log-trace = []
```

Build with multiple levels: `coffee build --features "log-info,log-debug"`

---

## The `[[bin]]` Section

The `[[bin]]` section defines binary targets. You can define multiple binary targets by repeating the `[[bin]]` table.

```toml
[[bin]]
name = "myapp"
src = ["src/main.c", "src/cli.c"]

[[bin]]
name = "mytool"
src = ["src/tool.c"]
```

### Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | String | Yes | Binary name (output filename). |
| `src` | Array of strings | Yes | Source file globs for this binary. |

---

## The `[test]` Section

The `[test]` section configures the test runner.

```toml
[test]
sources = ["tests/*.c"]
harness = "custom"
framework = "builtin"
```

### Fields

| Field | Type | Description |
|-------|------|-------------|
| `sources` | Array of strings | Test source file globs. Default: `["tests/*.c"]`. |
| `harness` | String | Test harness name (reserved for future use). |
| `framework` | String | Test framework name (reserved for future use). |

If `[test]` is not present, `coffee test` globs `tests/*.c` for test sources.

---

## The `[doc]` Section

The `[doc]` section configures documentation generation via `coffee doc`.

```toml
[doc]
project-name = "My Library"
output-dir = "docs/api"
input-dirs = ["src", "include"]
exclude-patterns = ["*/build/*", "*/tests/*"]
```

### Fields

| Field | Type | Description |
|-------|------|-------------|
| `project-name` | String | Project name for Doxygen. Default: package name. |
| `output-dir` | String | Output directory for generated docs. Default: `"docs/api"`. |
| `input-dirs` | Array of strings | Directories to scan for documentation comments. Default: `["src", "include"]`. |
| `exclude-patterns` | Array of strings | Glob patterns to exclude from scanning. |

If `[doc]` is not present and no `Doxyfile` exists, `coffee doc` prints a tip suggesting to run `doxygen -g` or add a `[doc]` section.

---

## Complete Example

A full `Coffee.toml` for a library with dependencies, features, binary targets, tests, and documentation:

```toml
[package]
name = "mylib"
version = "1.2.0"
edition = "c23"
description = "A high-performance JSON parser"
license = "MIT"
repository = "https://github.com/user/mylib"
authors = "Jane Doe <jane@example.com>"

[dependencies]
json-c = "1.0"
openssl = { git = "https://github.com/openssl/openssl.git", tag = "v3.0.0", optional = true }
local-utils = { path = "../utils" }

[features]
default = ["json", "logging"]
json = []
xml = []
logging = []
tls = ["openssl"]

[[bin]]
name = "mylib-cli"
src = ["src/cli/*.c"]

[[bin]]
name = "mylib-bench"
src = ["src/bench/*.c"]

[test]
sources = ["tests/*.c"]

[doc]
project-name = "MyLib"
output-dir = "docs/api"
input-dirs = ["src", "include"]
exclude-patterns = ["*/build/*"]
```

---

## Minimal Example

The smallest valid `Coffee.toml`:

```toml
[package]
name = "hello"
version = "0.1.0"
```

---

## Lockfile

Coffee generates a `Coffee.lock` file alongside `Coffee.toml` to pin exact dependency versions and git commit SHAs for reproducible builds. The lockfile is generated by `coffee generate-lockfile` or `coffee fetch` and should be committed to version control.

See `coffee help generate-lockfile` for details.

---

## Related Documentation

- [Feature System Guide](features.md) — Detailed guide on using features
- [Design Document](../DESIGN.md) — Architecture and design philosophy
