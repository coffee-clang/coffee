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

Keys are dot-separated paths into the config TOML: `build.jobs` is `jobs` in the `[build]` section, and a key
without a dot is a top-level key. `set` replaces the key in its own section (a new top-level key goes before the
first section header) and `unset` removes it from its own section only.

| Key | Default | Description |
|-----|---------|-------------|
| `registry.url` | see source | Registry base URL |
| `build.jobs` | 1 | Parallel build jobs |
| `build.cc` | clang | C compiler |
