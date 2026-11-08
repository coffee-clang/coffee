#!/bin/bash
# Per-function line coverage report. Run after `make coverage`.
# Usage: coverage-report.sh [threshold-percent]   (default 95)
# Lists the project functions below the threshold; vendored code
# (src/sds.c, src/toml.c, include/) is excluded.
set -eu
cd "$(dirname "$0")/.."

threshold="${1:-95}"
cov_tests="${COVERAGE_DIR:-bin/cov}/tests"

if [ ! -f "$cov_tests/test_support_unity.gcda" ]; then
	echo "Run 'make coverage' first" >&2
	exit 1
fi

report_dir=$(mktemp -d)
trap 'rm -rf "$report_dir"' EXIT

(cd "$report_dir" && gcov --json-format --stdout -o "$OLDPWD/$cov_tests" \
	"$OLDPWD/$cov_tests/test_support_unity.gcda" 2>/dev/null) |
	jq -r --argjson threshold "$threshold" '
		[.files[]
		 | select(.file | test("(^|/)(src/(sds|toml)\\.c|include/)") | not)
		 | select(.file | test("(^|/)src/") )
		 | .file as $file
		 | .lines | group_by(.function_name)[]
		 | select(.[0].function_name != null)
		 | {file: $file, name: .[0].function_name, total: length,
		    hit: (map(select(.count > 0)) | length)}] as $fns
		| ($fns | map(select(.hit * 100 < $threshold * .total)))
		| sort_by(.hit / .total)[]
		| "\(.file | sub("^src/\\.\\./"; "")):\(.name)  \(.hit)/\(.total) lines"
		, empty' |
	sort -u

echo "---"
(cd "$report_dir" && gcov --json-format --stdout -o "$OLDPWD/$cov_tests" \
	"$OLDPWD/$cov_tests/test_support_unity.gcda" 2>/dev/null) |
	jq -r '
		[.files[]
		 | select(.file | test("src/(sds|toml)\\.c|include/") | not)
		 | select(.file | test("src/"))
		 | .lines[]] as $l
		| "project lines: \([$l[] | select(.count > 0)] | length)/\($l | length)"'
