<div class="logo-container">
  <div class="logo">
    <img src="https://coffee-clang.github.io/logo.svg" alt="Coffee logo" width="120" height="120">
  </div>
  <div class="tagline">
    <h1>Coffee</h1>
    <p>A modern package manager for C.</p>
  </div>
</div>

Coffee is a package manager and build system for C projects. It manages dependencies,
orchestrates builds, and provides a consistent workflow — inspired by Cargo, but built
for the C ecosystem.

## Quick Start

```bash
# Create a new project
coffee new my-project

# Build it
cd my-project
coffee build

# Run tests
coffee test
```

## Project Structure

Coffee enforces a canonical structure on each project:

```
root
├── Coffee.toml         # Project manifest
├── include/            # Public API headers
├── src/                # Source files
├── deps/               # Dependencies
├── tests/              # Unit & integration tests
├── docs/               # Documentation
├── scripts/            # Utility scripts
└── build/              # Build artifacts (gitignored)
```

## Features

- **Manifest-driven builds** — `Coffee.toml` describes your project, its dependencies,
  and features
- **Dependency management** — Add, remove, and update dependencies with semver-aware
  resolution
- **Feature system** — Conditional compilation and optional dependencies via
  `[features]` in your manifest
- **Registry** — Search and install packages from the [Coffee registry](https://github.com/coffee-clang)
- **Static analysis** — `coffee check` runs clang-tidy and validates your manifest
- **Cross-compilation** — Target different platforms via `--target`

## Getting Started

### Installation

```bash
git clone https://github.com/coffee-clang/coffee.git
cd coffee
make
```

### Creating a Project

```bash
coffee new hello
cd hello
coffee build
./target/debug/hello
```

## Documentation

- **[Feature System Guide](features.md)** — Learn about Coffee's conditional compilation
  and optional dependency features
- **[Command Reference](commands/README.md)** — Detailed documentation for every command
- **[Design Document](TODO: link to DESIGN.md)** — Architecture and internals

## Project Status

Coffee is under active development. See the [TODO list](../TODO.md) for current status
and planned features.

## License

MIT — see the [LICENSE](../LICENSE) file.
