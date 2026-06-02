# info

Display the current package's information.

## Description

Prints detailed information about the current project from its Coffee.toml manifest, including name, version, edition, description, license, and dependencies.

## Usage

```
coffee info [package]
```

## Implementation Notes

- Reads and parses Coffee.toml from the current directory
- Displays all package metadata fields
- Lists all declared dependencies with their version constraints
