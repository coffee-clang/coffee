# doc

Generate documentation.

## Description

Generates API documentation from source code comments using Doxygen.

## Usage

11|9fd918cd ```
12|25754f48 coffee doc [generate|check]


15|25084983 ## Subcommands
16|00000000 
17|d75f14b9 | Subcommand | Description |
18|6d2d9b94 |------------|-------------|
19|944d461c | `generate` | (default) Generate Doxyfile from [doc] section and run doxygen |
20|511d166c | `check` | Verify that `doxygen` is installed |



22|ed232ad4 ## Configuration

The `[doc]` section in `Coffee.toml` can customize documentation:

```toml
[doc]
project-name = "My Project"
output-dir = "docs/api"
input-dirs = ["src", "include"]
exclude-patterns = ["*/build/*"]
```

33|34898ca1 If no `[doc]` section exists and no Doxyfile is present, `coffee doc` prints a tip
34|aef3e3d6 suggesting to run `doxygen -g` or add a `[doc]` section to `Coffee.toml`.
