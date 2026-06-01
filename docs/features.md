# Feature System Guide

## What Are Features?

Features are a way to enable or disable optional parts of a library or your own project. They are useful for:

- **Conditional Compilation**: Compile only the code you need, reducing binary size
- **Optional Dependencies**: Include dependencies only when their associated features are used
- **Customization**: Allow users of your library to choose which functionality to include
- **Testing**: Verify different configurations of your code
- **Platform-specific Code**: Enable features based on target platform

## Real-World Analogy

Think of building a car. You might have these options:

- **Sport Package**: adds spoiler, upgraded suspension, larger engine
- **Sunroof**: adds sliding glass roof and motor
- **Leather Seats**: premium interior
- **Advanced Safety**: collision detection, blind spot monitoring

You don't need all features for every customer. The feature system lets you "configure" your library (or the libraries you depend on) to include exactly what you need.

## Why This Matters for C

In C, conditional compilation is traditionally done with:

```c
#ifdef ENABLE_LOGGING
    printf("debug info\n");
#endif
```

But managing these flags manually is error-prone. Coffee's feature system automates:
- Defining which features exist
- Which code files correspond to which features
- Which optional dependencies are needed
- Passing `-D` flags to the compiler automatically

## Defining Features in Coffee.toml

### Basic Feature with No Dependencies

```toml
[package]
name = "mylib"
version = "1.0.0"

[features]
json = []           # Enable JSON support
xml = []            # Enable XML support
logging = []        # Enable debug logging
```

### Features with Dependencies

```toml
[features]
json = ["serde_json"]           # When json feature enabled, also enable serde_json's json feature
advanced-logging = ["log/max-level-debug", "regex"]  # Enable multiple dependency features

[package]
# This library depends on serde_json itself, but only when the json feature is enabled:
[dependencies]
serde_json = { version = "1.0", optional = true, features = ["json"] }
regex = { version = "1.0", optional = true }
log = { version = "0.4", optional = true, features = ["max-level-debug"] }
```

### Default Features

```toml
[features]
default = ["json", "logging"]   # These are enabled automatically unless --no-default-features

json = []
xml = []
logging = []
```

When a user does `coffee build`, they automatically get `json` and `logging`. To build without them:

```bash
coffee build --no-default-features
```

### Optional Dependencies

The `optional = true` flag on a dependency means: "Only link this library if one of my features requires it."

```toml
[dependencies]
openssl = { version = "3.0", optional = true }   # Only used if 'tls' feature is enabled

[features]
tls = ["openssl"]   # Enable 'tls' feature pulls in openssl
```

If a user builds without `--features tls`, OpenSSL won't be linked.

## Using Features in Your Code

### In Source Files

Use preprocessor conditionals:

```c
#include "mylib.h"

void do_something(bool use_json) {
    #ifdef FEATURE_JSON
    if (use_json) {
        // JSON-specific code
        json_serialize(...);
    }
    #endif

    // Always compiled code
    printf("Running\n");
}
```

Coffee converts feature names to uppercase and adds `FEATURE_` prefix:
- Feature `json` → `-DFEATURE_JSON`
- Feature `advanced-logging` → `-DFEATURE_ADVANCED_LOGGING`

### In Headers

You can provide different APIs based on features:

```c
// mylib.h
#ifdef FEATURE_JSON
void mylib_json_parse(const char *data);
#endif

#ifdef FEATURE_XML
void mylib_xml_parse(const char *data);
#endif
```

Users of your library can then conditionally call these functions:

```c
#ifdef FEATURE_JSON
mylib_json_parse(data);
#endif
```

## Consuming Libraries with Features

### Enabling Features

When you depend on a library that has features, you can choose which to enable:

```toml
[dependencies]
mylib = { version = "1.0", features = ["json", "logging"] }
```

Or from the command line:

```bash
coffee build --features "mylib/json,mylib/logging"
```

### Disabling Default Features

```toml
[dependencies]
mylib = { version = "1.0", default-features = false, features = ["xml"] }
```

Or:

```bash
coffee build --no-default-features --features "mylib/xml"
```

### Enabling All Features

```bash
coffee build --all-features
```

This enables ALL features from ALL dependencies. Useful for testing.

## Feature Resolution Algorithm

When you build with features, Coffee:

1. Starts with your explicitly requested features (from `--features` CLI flag or package dependencies)
2. Adds default features unless `--no-default-features` is set
3. Walks the dependency graph:
   - For each dependency, checks if any of its features are required
   - If `optional = true` and no features required, skip linking that dependency
   - If dependency requires specific features (via `dep/feature` syntax), add those to the set
4. **Unification**: If the same crate appears multiple times with different features, union them all
5. Generates `-DFEATURE_<NAME>` for each feature in the resolved set

### Example

Your `Coffee.toml`:

```toml
[dependencies]
a = "1.0"
b = "1.0"
```

Package `a` has `[features]`:
```toml
[features]
x = []
y = []
default = ["x"]
```

Package `b` has `[features]`:
```toml
[features]
z = []
```

You run:

```bash
coffee build --features "a/y"
```

**Resolution process:**

1. Root requests: `a/y`
2. Process dependency `a`: features needed: `y`
3. `a` has default `x`, but you explicitly requested `y`, so both `x` and `y` are enabled for `a`
4. Process dependency `b`: no features requested, so uses defaults (or none if no default)
5. Final feature set: `FEATURE_X`, `FEATURE_Y` (for a's code), nothing for b

## Conditional Compilation Patterns

### Feature-Specific Source Files

You can structure your project:

```
src/
├── main.c          # Always compiled
├── json.c          # #ifdef FEATURE_JSON
├── xml.c           # #ifdef FEATURE_XML
└── logging.c       # #ifdef FEATURE_LOGGING
```

Each `.c` file contains feature-gated code. Coffee will compile all `.c` files regardless, but they can conditionally compile code internally.

Alternatively, you can use build scripts to conditionally include files (future feature).

### Feature Detection

In your code, check if a feature is enabled:

```c
#ifdef FEATURE_JSON
#define HAS_JSON 1
#else
#define HAS_JSON 0
#endif

void mylib_init(void) {
    #if HAS_JSON
    init_json_subsystem();
    #endif
}
```

## Common Use Cases

### 1. Network vs CLI Tool

```toml
[features]
network = ["curl"]   # When building a networking tool
default = []        # By default, just CLI

[dependencies]
curl = { version = "7.0", optional = true }
```

Users who need networking can enable it. Others get smaller binary without linking curl.

### 2. Database Backend Selection

```toml
[features]
postgres = ["pq"]
mysql = ["mysqlclient"]
sqlite = []   # Built-in, no external dep

[dependencies]
pq = { version = "9.0", optional = true }
mysqlclient = { version = "8.0", optional = true }
```

User builds with PostgreSQL support:

```bash
coffee build --features "postgres"
```

### 3. Logging Levels

```toml
[features]
log-error = []
log-warn = []
log-info = []
log-debug = []
log-trace = []

# Can enable multiple:
# --features "log-info,log-debug" gives up to debug level
```

In code:

```c
#ifdef FEATURE_LOG_TRACE
#define LOG(fmt, ...) printf("[TRACE] " fmt "\n", ##__VA_ARGS__)
#elif defined(FEATURE_LOG_DEBUG)
#define LOG(fmt, ...) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#elif defined(FEATURE_LOG_INFO)
#define LOG(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG(fmt, ...) do {} while(0)
#endif
```

### 4. Testing Different Configurations

```bash
# Build with everything
coffee build --all-features

# Build minimal (only defaults)
coffee build

# Build without logging (smaller binary)
coffee build --no-default-features --features "json"

# Build with specific features
coffee build --features "json,xml,libmysqlclient"
```

## Best Practices

1. **Use lowercase, hyphenated names**: `advanced-logging`, `postgres-backend`
2. **Keep feature names stable**: Changing feature names breaks users' builds
3. **Document each feature**: In README, explain what each feature does
4. **Avoid feature overlap**: If two features enable mostly the same code, merge them
5. **Test all feature combinations**: At least test default + common combos
6. **Default to minimal**: Only enable useful defaults; let users opt-in to more
7. **Use `optional = true` wisely**: Mark dependencies optional only if they're truly feature-specific

## Troubleshooting

### "Feature 'x' not found"

- Check spelling: feature names are case-sensitive
- Ensure the dependency actually defines that feature in its Coffee.toml
- Use `coffee metadata` to inspect a package's available features (once implemented)

### "Cannot resolve features for dependency 'foo'"

- There's a circular dependency in feature requirements
- Check that you're not requesting a feature that doesn't exist
- The dependency might have been installed without its features metadata

### My code doesn't seem to be compiling conditionally

- Ensure `#ifdef FEATURE_NAME` matches the feature name (uppercase with underscores)
- Check that the feature is actually enabled (check build logs for `-DFEATURE_NAME`)
- Verify the code is in a `.c` file that's part of the build (all files in src/ are compiled)

## Limitations

- Features cannot currently enable/disable specific source files (only code within files)
- No support for target-specific features (e.g., `cfg(target_os = "linux")`)
- Build scripts (custom build logic per feature) not yet supported
- Workspace-level feature overrides not yet supported

## Future Enhancements

- Build script support: run custom code when features change
- `dev-dependencies` and `build-dependencies` with their own features
- Target-specific conditional compilation
- Workspace feature overrides at root level
- Feature linting to detect unused features

## Related Commands

- `coffee build` - Build with features
- `coffee test` - Run tests with features
- `coffee run` - Run binary with features
- `coffee metadata` - View package features (coming soon)
- `coffee generate-lockfile` - Generate lockfile with resolved features

## Example

See `examples/feature-demo/` for a complete working example.
