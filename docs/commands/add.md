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
- Auto-fetches the dependency from the registry
- Appends dependency flags to Makefile
- Creates symlink in deps/ directory
- Supports path, git, and registry dependencies
