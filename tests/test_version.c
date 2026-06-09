/*
 * Unit tests for version.c — version parsing and constraint checking.
 *
 * Covers:
 *   - version_satisfies with all operators (>=, <=, >, <, ==, !=)
 *   - Wildcard and null/empty constraints
 *   - Comma-separated AND constraints
 *   - Whitespace tolerance
 *   - Short versions (1, 1.2)
 *   - Edge cases: null args, malformed versions
 */

#include "../src/version.h"
#include "test_framework.h"

#include <stdio.h>
#include <string.h>

void coffee_register_version_tests(void);

/* ===================== WILDCARD AND NULL/EMPTY ===================== */

TEST(version_wildcard_satisfies_anything)
{
	ASSERT(version_satisfies("1.0.0", "*"), "wildcard constraint always satisfied");
	ASSERT(version_satisfies("9.9.9", "*"), "wildcard for high version");
	ASSERT(version_satisfies("0.0.0", "*"), "wildcard for zero version");
	PASS();
}

TEST(version_null_constraint_satisfied)
{
	ASSERT(version_satisfies("1.0.0", nullptr), "null constraint satisfied");
	PASS();
}

TEST(version_empty_constraint_satisfied)
{
	ASSERT(version_satisfies("1.0.0", ""), "empty constraint satisfied");
	PASS();
}

TEST(version_null_actual_satisfied)
{
	ASSERT(version_satisfies(nullptr, ">= 2.0"), "null actual satisfied");
	PASS();
}

/* ===================== EXACT MATCH (no operator) ===================== */

TEST(version_exact_match)
{
	ASSERT(version_satisfies("1.0.0", "1.0.0"), "exact match");
	ASSERT(version_satisfies("2.3.5", "2.3.5"), "exact match 2.3.5");
	PASS();
}

TEST(version_exact_mismatch)
{
	ASSERT(!version_satisfies("1.0.0", "1.0.1"), "exact mismatch");
	ASSERT(!version_satisfies("2.0.0", "1.9.9"), "exact mismatch 2 vs 1");
	ASSERT(!version_satisfies("1.1.0", "2.0.0"), "exact mismatch 1 vs 2");
	PASS();
}

/* ===================== >= OPERATOR ===================== */

TEST(version_ge_satisfied)
{
	ASSERT(version_satisfies("3.0.0", ">= 2.0"), ">= satisfied higher");
	ASSERT(version_satisfies("2.0.0", ">= 2.0"), ">= satisfied equal");
	ASSERT(version_satisfies("2.0.1", ">= 2.0.0"), ">= satisfied patch higher");
	ASSERT(version_satisfies("2.1.0", ">= 2.0.0"), ">= satisfied minor higher");
	PASS();
}

TEST(version_ge_not_satisfied)
{
	ASSERT(!version_satisfies("1.9.0", ">= 2.0"), ">= not satisfied");
	ASSERT(!version_satisfies("1.9.9", ">= 2.0.0"), ">= not satisfied 1.9.9");
	ASSERT(!version_satisfies("0.9.9", ">= 1.0"), ">= not satisfied 0.9.9");
	PASS();
}

/* ===================== <= OPERATOR ===================== */

TEST(version_le_satisfied)
{
	ASSERT(version_satisfies("2.3.0", "<= 2.4"), "<= satisfied lower");
	ASSERT(version_satisfies("2.4.0", "<= 2.4"), "<= satisfied equal");
	ASSERT(version_satisfies("2.3.9", "<= 2.4.0"), "<= satisfied patch lower");
	PASS();
}

TEST(version_le_not_satisfied)
{
	ASSERT(!version_satisfies("2.5.0", "<= 2.4"), "<= not satisfied");
	ASSERT(!version_satisfies("2.4.1", "<= 2.4.0"), "<= not satisfied patch");
	ASSERT(!version_satisfies("3.0.0", "<= 2.9.9"), "<= not satisfied major");
	PASS();
}

/* ===================== > OPERATOR ===================== */

TEST(version_gt_satisfied)
{
	ASSERT(version_satisfies("2.0.0", "> 1.0"), "> satisfied");
	ASSERT(version_satisfies("2.0.1", "> 2.0.0"), "> satisfied patch");
	PASS();
}

TEST(version_gt_not_satisfied)
{
	ASSERT(!version_satisfies("1.0.0", "> 1.0"), "> not satisfied equal");
	ASSERT(!version_satisfies("1.0.0", "> 2.0"), "> not satisfied lower");
	ASSERT(!version_satisfies("2.0.0", "> 2.0"), "> not satisfied equal 2.0");
	PASS();
}

/* ===================== < OPERATOR ===================== */

TEST(version_lt_satisfied)
{
	ASSERT(version_satisfies("1.0.0", "< 2.0"), "< satisfied");
	ASSERT(version_satisfies("2.3.9", "< 2.4.0"), "< satisfied patch");
	PASS();
}

TEST(version_lt_not_satisfied)
{
	ASSERT(!version_satisfies("3.0.0", "< 2.0"), "< not satisfied");
	ASSERT(!version_satisfies("2.0.0", "< 2.0"), "< not satisfied equal");
	ASSERT(!version_satisfies("2.0.1", "< 2.0.0"), "< not satisfied higher patch");
	PASS();
}

/* ===================== == OPERATOR ===================== */

TEST(version_eq_satisfied)
{
	ASSERT(version_satisfies("1.2.3", "== 1.2.3"), "== satisfied");
	ASSERT(version_satisfies("2.0.0", "== 2.0"), "== satisfied short constraint");
	PASS();
}

TEST(version_eq_not_satisfied)
{
	ASSERT(!version_satisfies("1.2.3", "== 1.2.4"), "== not satisfied");
	ASSERT(!version_satisfies("1.2.3", "== 1.3.3"), "== not satisfied minor");
	ASSERT(!version_satisfies("1.2.3", "== 2.2.3"), "== not satisfied major");
	PASS();
}

/* ===================== != OPERATOR ===================== */

TEST(version_ne_satisfied)
{
	ASSERT(version_satisfies("1.2.3", "!= 1.2.4"), "!= satisfied patch diff");
	ASSERT(version_satisfies("1.2.3", "!= 2.0.0"), "!= satisfied major diff");
	PASS();
}

TEST(version_ne_not_satisfied)
{
	ASSERT(!version_satisfies("1.2.3", "!= 1.2.3"), "!= not satisfied exact match");
	PASS();
}

/* ===================== COMMA-SEPARATED AND ===================== */

TEST(version_range_satisfied)
{
	ASSERT(version_satisfies("2.2.0", ">= 2.0, <= 2.4"), "range satisfied 2.2.0");
	ASSERT(version_satisfies("2.0.0", ">= 2.0, <= 2.4"), "range satisfied lower bound");
	ASSERT(version_satisfies("2.4.0", ">= 2.0, <= 2.4"), "range satisfied upper bound");
	ASSERT(version_satisfies("2.2.5", ">= 2.0, <= 2.4"), "range satisfied 2.2.5");
	PASS();
}

TEST(version_range_low_fail)
{
	ASSERT(!version_satisfies("1.9.0", ">= 2.0, <= 2.4"), "range fail too low");
	ASSERT(!version_satisfies("1.9.9", ">= 2.0, <= 2.4"), "range fail 1.9.9");
	PASS();
}

TEST(version_range_high_fail)
{
	ASSERT(!version_satisfies("2.5.0", ">= 2.0, <= 2.4"), "range fail too high");
	ASSERT(!version_satisfies("3.0.0", ">= 2.0, <= 2.4"), "range fail 3.0.0");
	PASS();
}

/* ===================== WHITESPACE TOLERANCE ===================== */

TEST(version_whitespace_tolerance)
{
	ASSERT(version_satisfies("2.2.0", ">=   2.0 ,  <=  2.4"), "whitespace tolerance");
	ASSERT(version_satisfies("2.0.0", ">=2.0"), "no space before version");
	ASSERT(version_satisfies("2.0.0", ">= 2.0"), "space before version");
	ASSERT(version_satisfies("1.0.0", "  ==   1.0.0  "), "extra spaces everywhere");
	PASS();
}

/* ===================== SHORT VERSIONS ===================== */

TEST(version_short_versions)
{
	/* Actual version is short */
	ASSERT(version_satisfies("1", ">= 1"), "short actual '1'");
	ASSERT(version_satisfies("1", ">= 1.0"), "short actual vs longer constraint");
	ASSERT(version_satisfies("1", ">= 1.0.0"), "short actual vs full constraint");
	ASSERT(version_satisfies("1", "== 1"), "short exact match");
	ASSERT(!version_satisfies("1", ">= 2"), "short actual fails");

	/* Constraint version is short */
	ASSERT(version_satisfies("1.0.0", ">= 1"), "short constraint '>= 1'");
	ASSERT(version_satisfies("1.5.0", ">= 1"), "short constraint satisfied");
	ASSERT(version_satisfies("1.0.0", "== 1"), "short constraint == 1");
	PASS();
}

TEST(version_two_component)
{
	ASSERT(version_satisfies("2.1", ">= 2.1"), "two-comp actual");
	ASSERT(version_satisfies("2.1.0", ">= 2.1"), "three-comp actual vs two-comp constraint");
	ASSERT(version_satisfies("2.1", ">= 2.1.0"), "two-comp actual vs three-comp constraint");
	ASSERT(!version_satisfies("2.1", ">= 2.2"), "two-comp fail");
	PASS();
}

/* ===================== EDGE CASES ===================== */

TEST(version_zero_version)
{
	ASSERT(version_satisfies("0.0.0", ">= 0.0.0"), "zero >= zero");
	ASSERT(version_satisfies("0.0.0", "<= 0.0.0"), "zero <= zero");
	ASSERT(version_satisfies("0.0.0", "== 0.0.0"), "zero == zero");
	ASSERT(!version_satisfies("0.0.0", "!= 0.0.0"), "zero != zero fails");
	ASSERT(!version_satisfies("0.0.0", "> 0.0.0"), "zero > zero fails");
	ASSERT(!version_satisfies("0.0.0", "< 0.0.0"), "zero < zero fails");
	PASS();
}

TEST(version_large_versions)
{
	ASSERT(version_satisfies("99999.99999.99999", ">= 99999.99999.99999"), "large >= satisfied");
	ASSERT(version_satisfies("99999.99999.99999", "<= 99999.99999.99999"), "large <= satisfied");
	ASSERT(version_satisfies("99999.99999.99999", "== 99999.99999.99999"), "large == satisfied");
	PASS();
}

TEST(version_malformed_actual)
{
	/* Malformed actual versions should pass (warning emitted) */
	ASSERT(version_satisfies("not-a-version", ">= 1.0"), "malformed actual passes");
	ASSERT(version_satisfies("", ">= 1.0"), "empty actual passes");
	PASS();
}

TEST(version_malformed_constraint)
{
	/* Malformed constraint segments should pass (warning emitted) */
	ASSERT(version_satisfies("1.0.0", ">= not-a-version"), "malformed constraint passes");
	PASS();
}

TEST(version_malformed_in_range)
{
	/* One bad segment in a range — the good one should still be checked */
	ASSERT(version_satisfies("2.2.0", ">= 2.0, xxx, <= 2.4"), "bad segment in range passes");
	PASS();
}

TEST(version_single_equal_operator)
{
	/* Single '=' is treated as '==' */
	ASSERT(version_satisfies("1.0.0", "= 1.0.0"), "single = treated as ==");
	ASSERT(!version_satisfies("1.0.1", "= 1.0.0"), "single = mismatch");
	PASS();
}

TEST(version_multiple_constraints_and)
{
	ASSERT(version_satisfies("2.0.0", ">= 1.0, >= 1.5, >= 2.0"), "three >= AND-ed");
	ASSERT(!version_satisfies("1.4.0", ">= 1.0, >= 1.5, >= 2.0"), "three >= fail middle");
	ASSERT(version_satisfies("2.4.0", ">= 2.0, <= 3.0, != 2.5.0"), "three mixed AND-ed");
	ASSERT(!version_satisfies("2.5.0", ">= 2.0, <= 3.0, != 2.5.0"), "three mixed fail != ");
	PASS();
}

TEST(version_constraint_only_whitespace)
{
	ASSERT(version_satisfies("1.0.0", "   "), "whitespace-only constraint satisfied");
	PASS();
}

TEST(version_operator_no_version)
{
	/* ">=" with nothing after should pass (tolerant) */
	ASSERT(version_satisfies("1.0.0", ">="), ">= with no version passes");
	ASSERT(version_satisfies("1.0.0", ">= "), ">= with trailing whitespace passes");
	PASS();
}

void coffee_register_version_tests(void)
{
	TEST_REGISTER(version_wildcard_satisfies_anything);
	TEST_REGISTER(version_null_constraint_satisfied);
	TEST_REGISTER(version_empty_constraint_satisfied);
	TEST_REGISTER(version_null_actual_satisfied);
	TEST_REGISTER(version_exact_match);
	TEST_REGISTER(version_exact_mismatch);
	TEST_REGISTER(version_ge_satisfied);
	TEST_REGISTER(version_ge_not_satisfied);
	TEST_REGISTER(version_le_satisfied);
	TEST_REGISTER(version_le_not_satisfied);
	TEST_REGISTER(version_gt_satisfied);
	TEST_REGISTER(version_gt_not_satisfied);
	TEST_REGISTER(version_lt_satisfied);
	TEST_REGISTER(version_lt_not_satisfied);
	TEST_REGISTER(version_eq_satisfied);
	TEST_REGISTER(version_eq_not_satisfied);
	TEST_REGISTER(version_ne_satisfied);
	TEST_REGISTER(version_ne_not_satisfied);
	TEST_REGISTER(version_range_satisfied);
	TEST_REGISTER(version_range_low_fail);
	TEST_REGISTER(version_range_high_fail);
	TEST_REGISTER(version_whitespace_tolerance);
	TEST_REGISTER(version_short_versions);
	TEST_REGISTER(version_two_component);
	TEST_REGISTER(version_zero_version);
	TEST_REGISTER(version_large_versions);
	TEST_REGISTER(version_malformed_actual);
	TEST_REGISTER(version_malformed_constraint);
	TEST_REGISTER(version_malformed_in_range);
	TEST_REGISTER(version_single_equal_operator);
	TEST_REGISTER(version_multiple_constraints_and);
	TEST_REGISTER(version_constraint_only_whitespace);
	TEST_REGISTER(version_operator_no_version);
}
