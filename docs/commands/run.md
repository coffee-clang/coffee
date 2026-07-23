# run

Build and execute the project.

## Description

Compiles and runs the binary. Shortcut for \`coffee build && ./build/debug/<name>\`.

## Usage

```
coffee run [options] [-- <args>...]
```

## Options

- `--release` - Build in release mode
- `--debug` - Build with debug symbols

## Implementation Notes

- First calls build_project()
- Then executes the compiled binary
- Passes remaining arguments to the program
