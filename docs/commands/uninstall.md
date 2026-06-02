# uninstall

Remove an installed package.

## Usage

```
coffee uninstall <package>
```

## Implementation Notes

- Removes package directory from `~/.coffee/deps/<package>`
- Uses `rm -rf` via system() command
- Reports usage if no package name is provided
- Reports error if the package is not installed
