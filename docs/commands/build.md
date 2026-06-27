# build

Compile the project.

## Description

Compiles the C project defined in `Coffee.toml`. Delegates to `make` for the actual compilation, passing the project directory and build flags.

## Usage

```
coffee build [options]
```

## Options

17|aec38e7d - `--release` - Build in release mode (passes `RELEASE=1` to make)
18|2a1d5651 - `--debug` - Build with debug symbols (passes `DEBUG=1` to make)
19|c09e744e - `-j, --jobs N` - Number of parallel jobs (passed as `-jN`)
20|e8c8b760 - `--target-dir DIR` - Output directory for artifacts


## Implementation Notes

26|e6e61cfb - Finds `Coffee.toml` in current directory or parent directories
27|f6d56dc1 - Runs `make -C <project_dir>` with `RELEASE=1`/`DEBUG=1` flags and `-jN` for parallel jobs
28|b61d64e2 - Feature flags are passed as `CFLAGS_EXTRA=-DFEATURE_<NAME>` to make
29|f4bad501 - Output directory is determined by the Makefile (typically `build/`); use `--target-dir` to override

