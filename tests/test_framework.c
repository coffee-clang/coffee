#include "test_framework.h"

#include "../src/strings.h"

#include <sys/wait.h>
#include <unistd.h>

/* Global test registry */
test_entry_t test_framework_tests[TEST_FRAMEWORK_MAX_TESTS];
u64          test_framework_count = 0;

/* Per-run results */
static i64 tests_run    = 0;
static i64 tests_passed = 0;
static i64 tests_failed = 0;

void test_framework_register(const char *name, test_func_t func)
{
	if (test_framework_count >= TEST_FRAMEWORK_MAX_TESTS) {
		fprintf_safe(stderr, "Error: too many tests (max %d)\n", TEST_FRAMEWORK_MAX_TESTS);
		return;
	}
	test_framework_tests[test_framework_count].name = name;
	test_framework_tests[test_framework_count].func = func;
	test_framework_count++;
}

static i64 test_matches_filter(const char *test_name, const char *filter)
{
	if (!filter || filter[0] == '\0') {
		return 1; /* No filter = match all */
	}
	return strstr(test_name, filter) != nullptr;
}

i64 test_framework_run(const char *filter)
{
	i64 local_passed = 0;
	i64 local_failed = 0;

	for (u64 i = 0; i < test_framework_count; i++) {
		const char *name = test_framework_tests[i].name;

		if (!test_matches_filter(name, filter)) {
			continue;
		}

		printf("  %-55s ... ", name);
		fflush(stdout);

		/* COFFEE_TEST_NO_FORK=1 runs tests in-process.  The forked child
		 * _exit()s, which skips LeakSanitizer's atexit leak check; the
		 * no-fork pass restores leak detection (a crashing test kills
		 * the suite, which is the point of that dedicated pass). */
		const char *no_fork = getenv("COFFEE_TEST_NO_FORK");
		if (no_fork != nullptr && strcmp(no_fork, "1") == 0) {
			/* Tests that chdir() and then fail return early without
			 * restoring the cwd; restore it here so the in-process
			 * pass stays deterministic. */
			char old_cwd[4096];
			bool have_cwd = getcwd(old_cwd, sizeof(old_cwd)) != nullptr;
			i64   result  = test_framework_tests[i].func();
			if (have_cwd && chdir(old_cwd) != 0) {
				fprintf_safe(stderr, "Error: cannot restore cwd to '%s' after test '%s'\n", old_cwd, name);
				result = 0;
			}
			if (result) {
				local_passed++;
			} else {
				local_failed++;
			}
			tests_run++;
			continue;
		}

		/* Run each test in a forked child so a segfault or abort in one
		 * test cannot kill the remaining tests or corrupt the counters.
		 * The child reports its result via the exit code; PASS()/FAIL()
		 * print the verdict before returning, and _exit() skips the
		 * parent's atexit handlers and buffered output. */
		pid_t pid = fork();
		if (pid == 0) {
			i64 result = test_framework_tests[i].func();
			_exit(result ? 0 : 1);
		}

		if (pid > 0) {
			int wstatus;
			waitpid(pid, &wstatus, 0);
			if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0) {
				local_passed++;
			} else {
				if (WIFSIGNALED(wstatus)) {
					printf("  CRASHED (signal %d)\n", WTERMSIG(wstatus));
				}
				local_failed++;
			}
		} else {
			perror("fork");
			local_failed++;
		}

		tests_run++;
	}

	tests_passed += local_passed;
	tests_failed += local_failed;

	return local_failed > 0 ? 1 : 0;
}

void test_framework_summary(void)
{
	printf("\n=== Results: %lld passed, %lld failed, %lld total ===\n", (long long)tests_passed,
	       (long long)tests_failed, (long long)tests_run);
}

void test_framework_reset(void)
{
	test_framework_count = 0;
	tests_run            = 0;
	tests_passed         = 0;
	tests_failed         = 0;
}
