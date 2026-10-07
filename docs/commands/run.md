# run

Build and execute the project.

## Description

Builds the project (same as `coffee build`, including Makefile generation) and then executes the compiled
binary. Shortcut for `coffee build && ./build/<name>`, with the binary resolved against the project
directory.

## Usage

```
coffee run [options] [-- <args>...]
```

## Options

- `--release` - Build in release mode
- `--debug` - Build with debug symbols
- `--target-dir DIR` - Output directory for artifacts (default `build`)

## Implementation Notes

- Runs the shared build routine (generates the Makefile unless `[compilation] Makefile = "legacy"`, then
  runs `make` on the default target `all`)
- Then executes the compiled binary at `<project_dir>/build/<name>` (or `--target-dir`), resolved against
  the project directory — so `coffee run` works from any subdirectory
- Passes remaining arguments to the program