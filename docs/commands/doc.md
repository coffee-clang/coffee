# doc

Generate documentation.

## Description

Generates API documentation from source code comments using Doxygen.

## Usage

```
coffee doc [generate|check]
```

## Subcommands

| Subcommand | Description |
|------------|-------------|
| `generate` | (default) Generate Doxyfile from [doc] section and run doxygen |
| `check` | Verify that `doxygen` is installed |

## Configuration

The `[doc]` section in `Coffee.toml` customizes documentation generation. See the [doc section in the Coffee.toml specification](../coffee-toml.md#the-doc-section) for available fields.

Documentation is always written to the `doc/` directory (Doxygen HTML output lands in
`doc/html/`). The generated `.gitignore` ignores `doc/`.

If no `[doc]` section exists and no Doxyfile is present, `coffee doc` prints a tip
suggesting to run `doxygen -g` or add a `[doc]` section to `Coffee.toml`.
