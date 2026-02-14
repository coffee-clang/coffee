# new

Create a new C project in a new directory.

## Description

Creates a new C project with a `Coffee.toml` manifest file and a basic source file structure.

## Usage

```
coffee new <path>
```

## Examples

```bash
# Create a new project in mylib/
coffee new mylib

# Create in current directory
coffee new .
```

## Implementation Notes

- Creates `Coffee.toml` with default template
- Creates `src/main.c` with Hello World
- Validates project name (no special characters)
- Checks if directory already exists
