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
- Downloads each package to ~/.coffee/deps/
- Does not invoke compiler
