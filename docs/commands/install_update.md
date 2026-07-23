# install-update

Update installed packages to the latest versions.

## Description

Updates globally installed packages by running `git pull --ff-only` in each
`~/.coffee/deps/<name>` directory. Can update all packages or a specific one.

## Usage

```
coffee install-update [package]
```

## Implementation Notes

- Updates packages in `~/.coffee/deps/`
- Runs `git pull --ff-only` in each package directory
- If a package name is specified, updates only that one
- Uses the `~/.coffee/deps/` global install cache

- If no package is specified, updates all installed packages
