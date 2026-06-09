# install-update-config

Install or update the Coffee configuration file with default values.

## Description

Bootstraps `~/.coffee/config.toml` if it does not exist, and ensures all expected configuration keys are present. Missing keys are added with sensible defaults; existing user settings are never overwritten.

## Usage

```
coffee install-update-config
```

## Defaults

| Key | Default | Description |
|-----|---------|-------------|
| `default-target` | `"debug"` | Default build target when not specified |
| `default-edition` | `"c23"` | Default C standard edition |
| `build.jobs` | `"0"` | Parallel build jobs (0 = auto-detect) |
| `registry.index-url` | `"https://coffee-clang.github.io/recipes/.well-known/packages.json.zstd"` | Registry index URL |

## Implementation Notes

- Creates `~/.coffee/` directory if it doesn't exist
- Reads existing config line-by-line to preserve formatting and comments
- Inserts missing keys in the appropriate `[section]` block
- Reports "Configuration is up-to-date." if no changes were needed
