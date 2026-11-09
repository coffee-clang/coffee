#!/bin/bash
# Coverage report. Run after `make coverage`.
# Usage: coverage-report.sh [threshold-percent]   (default 95)
# Lists the project functions below the threshold (line coverage), prints the
# project line and branch totals, and exits 1 when branch coverage is below
# the threshold.  Vendored code (src/sds.c, src/toml.c, include/) is excluded.
set -eu
cd "$(dirname "$0")/.."

threshold="${1:-95}"
cov_tests=$(realpath -m "${COVERAGE_DIR:-bin/cov}/tests")

if [ ! -f "$cov_tests/test_support_unity.gcda" ]; then
	echo "Run 'make coverage' first" >&2
	exit 1
fi

report_dir=$(mktemp -d)
trap 'rm -rf "$report_dir"' EXIT

(cd "$report_dir" && gcov --json-format --branch-probabilities --stdout -o "$cov_tests" \
	"$cov_tests/test_support_unity.gcda" 2>/dev/null) >"$report_dir/gcov.json"

# The project files of the report: sources under src/, minus vendored code.
project='[.files[]
	| select(.file | test("(^|/)(src/(sds|toml)\\.c|include/)") | not)
	| select(.file | test("(^|/)src/"))]'

jq -r --argjson threshold "$threshold" "$project"' as $files
	| [$files[]
	   | .file as $file
	   | .lines | group_by(.function_name)[]
	   | select(.[0].function_name != null)
	   | {file: $file, name: .[0].function_name, total: length,
	      hit: (map(select(.count > 0)) | length)}]
	| map(select(.hit * 100 < $threshold * .total))
	| sort_by(.hit / .total)[]
	| "\(.file | sub("^src/\\.\\./"; "")):\(.name)  \(.hit)/\(.total) lines"' \
	"$report_dir/gcov.json" | sort -u

echo "---"
jq -r "$project"' as $files
	| [$files[].lines[]] as $l
	| [$l[].branches[]] as $b
	| "project lines: \([$l[] | select(.count > 0)] | length)/\($l | length)",
	  "project branches: \([$b[] | select(.count > 0)] | length)/\($b | length)"' \
	"$report_dir/gcov.json"

read -r hit total < <(jq -r "$project"' | [.[].lines[].branches[]]
	| "\([.[] | select(.count > 0)] | length) \(length)"' "$report_dir/gcov.json")
pct=$(awk -v h="$hit" -v t="$total" 'BEGIN { if (t > 0) { printf "%.1f", 100 * h / t } else { print "0.0" } }')
echo "branch coverage: $pct% (threshold $threshold%)"
if [ "$total" -eq 0 ] || [ $((hit * 100)) -lt $((threshold * total)) ]; then
	echo "Error: branch coverage $pct% is below $threshold%" >&2
	exit 1
fi
