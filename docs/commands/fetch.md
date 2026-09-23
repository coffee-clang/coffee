# fetch

Download dependencies without building.

## Description

Downloads all dependencies to the local cache without compiling the project.

## Usage

```
coffee fetch
```

## Implementation Notes

- Reads dependencies from Coffee.toml
- Dependency URLs must use an allow-listed scheme (`https`, `git`, `ssh`, `git+ssh`, `git+https`),
  a local path, or scp-style ssh; `http` is rejected
- Downloads each package to ~/.coffee/deps/
- Does not invoke compiler
