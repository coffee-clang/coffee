# machete

Detect unused dependencies.

## Description

Scans source files for `#include` directives and reports which declared dependencies are not actually used.

## Usage

```
coffee machete
```

## Implementation Notes

- Walks source directories recursively (src, include, tests)
- Matches #include lines against dependency names
- Reports dependencies that appear declared but never included
