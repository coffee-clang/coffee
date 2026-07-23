# grep

Search for patterns in the project's source code.

## Description

7|de34f330 Searches for a regex pattern across all source files in \`src/\` and \`tests/\`, excluding \`build/\` and \`.git/\`.

## Usage

```
coffee grep <pattern>
```

## Implementation Notes

- Uses `grep -rn` under the hood
- Excludes build/ and .git/ directories
- Searches all .c and .h files in src/ and tests/
