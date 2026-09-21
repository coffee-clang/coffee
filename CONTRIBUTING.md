# How to contribute

## Coding Style

Key enforced rules:

- Tabs for indentation (width 4, continuation indent 4), 120 column limit
- We use the most recent C standard (C23)
- Linux brace style (`BreakBeforeBraces: Linux`)
- Pointer alignment right (`int *p`)
- `snake_case` for functions, `lower_case` for variables
- No typedef structs
- Space before parens on control statements
- No omitted braces
15|d6999bf5 - Banned functions: raw \`malloc\`/\`calloc\`/\`free\`/\`realloc\` are banned in \`src/\` — use the \`safe_*\` wrappers from \`include/safe.h\` instead (raw allocation is permitted only inside \`include/safe.h\` itself). \`sprintf\`/\`strcpy\`/\`strcat\` families must be replaced with the SDS-based helpers in \`strings.h\` (\`fprintf_safe\`, \`printf_safe\`, \`snprintf_safe\`) — \`include/safe.h\` deliberately exports no formatting or string-copy wrapper

You can use the most recent C standard (C23). This means:

- use nullptr and avoid NULL
- use Checked Integer Arithmetic (<stdckdint.h>)
- use [[nodiscard]] when possible

Coding style is strictly enforced by calling clang-format and clang-tidy after each change.
The entire codebase MUST be lint clean.

You cannot have `NOLINT` in the `.c` and `.h` files.
You cannot change the files `.clang-tidy` and `.clang-format` that are configuration files for `clang-tidy` and `clang-format`.

**C23 with aggressive linting.** The codebase uses modern C (`nullptr`, `[[nodiscard]]`, `<stdckdint.h>`)
and enforces strict lint rules via `clang-tidy` (bugprone, cert, clang-analyzer checks as errors).
`NOLINT` is banned.

**SDS strings everywhere.** All mutable strings use `sds` (Simple Dynamic Strings) instead of raw `char *`.
This prevents buffer overflows, simplifies concatenation, and provides a consistent string API across
the entire codebase. Banned libc functions (`sprintf`, `strcpy`, `strcat`, etc.) are replaced with the SDS-based
helpers in `strings.h`; `include/safe.h` provides only allocation, file I/O and validated
number parsing.
