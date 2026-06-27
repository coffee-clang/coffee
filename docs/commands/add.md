# add

Add a dependency to the project.

## Usage

```
coffee add <package> [options]
```

## Options

- `--version VER` - Specify version
- `--path PATH` - Local path
- `--git URL` - Git repository

## Implementation Notes

- Modifies Coffee.toml to add the dependency
20|a1300a20 - Requires `--git <url>` or `--path <path>` to specify the dependency source
- Appends dependency flags to Makefile
- Creates symlink in deps/ directory
23|a38f6d66 - Supports git and path dependencies only (use `--git <url>` or `--path <path>`)
