# build

Compile the project.

## Description

Compiles the C project defined in `Coffee.toml`. Delegates to `make` for the actual compilation, passing the project directory and build flags.

## Usage

```
coffee build [options]
```

## Options

- `--release` - Build in release mode (passes `RELEASE=1` to make)
- `--debug` - Build with debug symbols (passes `DEBUG=1` to make)
- `-j, --jobs N` - Number of parallel jobs (passed as `-jN`)
- `--target-dir DIR` - Output directory for artifacts


## Implementation Notes

- Finds `Coffee.toml` in current directory or parent directories
- Runs `make -C <project_dir>` with `RELEASE=1`/`DEBUG=1` flags and `-jN` for parallel jobs
- Feature flags are passed as `CFLAGS_EXTRA=-DFEATURE_<NAME>` to make
- Output directory is determined by the Makefile (typically `build/`); use `--target-dir` to override

