#!/bin/bash
# Aggregate coverage from gcov files
# Usage: agg-cov.sh <directory>
DIR="${1:-.}"
total=0
covered=0
for gcov_f in "$DIR"/*.gcov; do
    [ -f "$gcov_f" ] || continue
    while IFS= read -r line; do
        first=$(echo "$line" | awk '{print $1}')
        case "$first" in
            [0-9]*) covered=$((covered + 1)); total=$((total + 1)) ;;
            -|#) ;;
            *) total=$((total + 1)) ;;
        esac
    done < "$gcov_f"
done
echo "Total: $total, Covered: $covered, Pct: $((covered * 100 / total))%"
