# build

Compile the project.

## Description

Compiles the C project defined in `Coffee.toml`. Uses clang/gcc to compile source files.

## Usage

```
coffee build [options]
```

## Options

- `--release` - Build in release mode with optimizations
- `--debug` - Build with debug symbols
- `--target TRIPLE` - Target for cross-compilation
- `-j, --jobs N` - Number of parallel jobs
- `--target-dir DIR` - Output directory for artifacts

## Implementation Notes

- Finds `Coffee.toml` in current directory or parent directories
- Invokes compiler via subprocess (clang or gcc)
- Outputs to `target/debug/` or `target/release/`
- Currently compiles all `src/*.c` files
