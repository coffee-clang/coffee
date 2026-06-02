# config

Manage configuration values.

## Usage

```
coffee config list
coffee config get <key>
coffee config set <key> <value>
coffee config unset <key>
```

Configuration is stored in `~/.coffee/config.toml`.

## Examples

```
coffee config set build.jobs 8
coffee config get build.jobs
coffee config --list
coffee config unset build.jobs
```

## Keys

Keys are dot-separated paths into the config TOML:

| Key | Default | Description |
|-----|---------|-------------|
| `registry.url` | see source | Registry base URL |
| `build.jobs` | 1 | Parallel build jobs |
| `build.cc` | clang | C compiler |
