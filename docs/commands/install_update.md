# install-update

Update installed packages to the latest versions.

## Description

7|d11a6d45 Updates globally installed packages by running `git pull --ff-only` in each
8|6b5934a8 `~/.coffee/deps/<name>` directory. Can update all packages or a specific one.

## Usage

```
coffee install-update [package]
```

## Implementation Notes

17|85bc7de7 - Updates packages in `~/.coffee/deps/`
18|36dc88c4 - Runs `git pull --ff-only` in each package directory
19|afcd5c09 - If a package name is specified, updates only that one
20|136e70c2 - Uses the `~/.coffee/deps/` global install cache

- If no package is specified, updates all installed packages
