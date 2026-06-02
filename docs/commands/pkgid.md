# pkgid

Print the fully qualified package identifier.

## Description

Prints the current project's package spec in `name:version` format.

## Usage

```
coffee pkgid
```

## Examples

```
myproject:0.1.0
```

## Implementation Notes

- Reads name and version from Coffee.toml
- Outputs: `name:version`
- Defaults to `project:0.1.0` if manifest fields are missing
