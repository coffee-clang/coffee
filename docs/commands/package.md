# package

Assemble the project into a distributable archive.

## Description

Creates a tar.gz archive of the project source code, suitable for distribution.

## Usage

```
coffee package
```

## Implementation Notes

- Outputs to `target/package/<name>-<version>.tar.gz`
- Archives Coffee.toml, src/, and tests/ directories
- Creates target/ and target/package/ directories if needed
