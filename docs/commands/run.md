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

- Requires a Makefile; builds with `make -C <project_dir> [RELEASE=1] [DEBUG=1] [-j<N>] [CFLAGS_EXTRA=...]`
- Then executes the compiled binary from `build/debug/<name>` (or `--target-dir`)
- Passes remaining arguments to the program
