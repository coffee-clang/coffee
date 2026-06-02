# grep

Search for patterns in the project's source code.

## Description

Searches for a regex pattern across all source files in `src/` and `tests/`, excluding `target/` and `.git/`.

## Usage

```
coffee grep <pattern>
```

## Implementation Notes

- Uses `grep -rn` under the hood
- Excludes target/ and .git/ directories
- Searches all .c and .h files in src/ and tests/
