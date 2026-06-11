#!/bin/bash
set -e
COV_DIR="bin/cov"
CC="clang"
CFLAGS="-g -Wall -Wextra -O0 -std=c23 -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes -pedantic -Wconversion -Wsign-conversion -Wunused -Wunused-function -Wunused-parameter -Wfloat-equal -Wundef -Wmissing-declarations -Wfloat-equal -Wundef -Wmissing-include-dirs -Wmultichar -Wsystem-headers -Wno-gnu-include-next -Wformat=2 -Wformat-security -Wnonnull -D_GNU_SOURCE -include src/compat_limits.h -iquotesrc -isystemdeps -isystemdeps/toml -fno-function-sections -fno-data-sections -fasynchronous-unwind-tables -fno-common -fdebug-macro -fno-delete-null-pointer-checks -fno-strict-overflow -fno-strict-aliasing -fwrapv -fno-omit-frame-pointer -fstack-protector-strong --coverage -O0"
LDFLAGS="-static /usr/lib/x86_64-linux-gnu/libz.a --coverage"

rm -rf "$COV_DIR"

mkdir -p "$COV_DIR/src/commands" "$COV_DIR/tests"

# Compile source files
for f in src/*.c src/commands/*.c; do
    out="$COV_DIR/${f%.c}.o"
    mkdir -p "$(dirname "$out")"
    if [ "$f" = "src/coffee.c" ]; then
        $CC $CFLAGS -DCOFFEE_TEST_RUNNER -c "$f" -o "$out" 2>/dev/null || true
    else
        $CC $CFLAGS -c "$f" -o "$out" 2>/dev/null || true
    fi
done


# Compile test files
for f in tests/*.c; do
    out="$COV_DIR/${f%.c}.o"
    mkdir -p "$(dirname "$out")"
    $CC $CFLAGS -DCOFFEE_TEST_RUNNER -Itests -c "$f" -o "$out" 2>/dev/null || true
done

# Link
OBJS=$(find "$COV_DIR" -name '*.o' ! -name 'test_main.o')
$CC $LDFLAGS -o "$COV_DIR/runner" $OBJS 2>&1 | tail -5

echo "Coverage runner built at $COV_DIR/runner"
