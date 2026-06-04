#!/bin/bash
# Generate per-file coverage report after running `make coverage`
set -e
cd "$(dirname "$0")/.."

COV_DIR=bin/cov
if [ ! -d "$COV_DIR" ]; then
    echo "Run 'make coverage' first" >&2
    exit 1
fi

cd "$COV_DIR"

# Run gcov for all source files
for f in ../../src/*.c ../../src/commands/*.c ../../src/*.h; do
    llvm-cov gcov "$f" 2>/dev/null || true
done

echo "=== Per-file Line Coverage ==="
echo ""

total_all=0
covered_all=0

for gcov_f in *.c.gcov; do
    [ -f "$gcov_f" ] || continue
    total=0
    covered=0
    while IFS= read -r line; do
        first=$(echo "$line" | awk '{print $1}')
        case "$first" in
            [0-9]*) covered=$((covered + 1)); total=$((total + 1)) ;;
            -|#) ;;
            *) total=$((total + 1)) ;;
        esac
    done < "$gcov_f"
    if [ "$total" -gt 0 ]; then
        pct=$((covered * 100 / total))
        printf "  %-40s %3d%% (%d/%d)\n" "$(basename "$gcov_f" .c.gcov).c" "$pct" "$covered" "$total"
        total_all=$((total_all + total))
        covered_all=$((covered_all + covered))
    fi
done

echo ""
if [ "$total_all" -gt 0 ]; then
    pct_all=$((covered_all * 100 / total_all))
    printf "  %-40s %3d%% (%d/%d)\n" "TOTAL" "$pct_all" "$covered_all" "$total_all"
fi
