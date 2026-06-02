# doc

Generate documentation.

## Description

Generates API documentation from source code comments using Doxygen.

## Usage

```
coffee doc [--no-deps]
```

## Options

| Option | Description |
|--------|-------------|
| `--no-deps` | Skip documentation for dependencies |

## Configuration

The `[doc]` section in `Coffee.toml` can customize documentation:

```toml
[doc]
project-name = "My Project"
output-dir = "docs/api"
input-dirs = ["src", "include"]
exclude-patterns = ["*/build/*"]
```

If no `[doc]` section exists, `coffee doc` falls back to running `doxygen -g`.
