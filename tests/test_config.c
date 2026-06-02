#include "../src/coffee.h"
#include "../src/strings.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

/*
 * Config tests use a temporary COFFEE_HOME to avoid sharing state.
 * Each test sets COFFEE_HOME before running and restores it after.
 * Verification is done via direct file reads (not toml parsing) to avoid
 * coupling test success to the TOML library behavior.
 */

static char saved_coffee_home[4096];

static void set_test_home(const char *test_name)
{
	const char *old = getenv("COFFEE_HOME");
	if (old) {
		snprintf_safe(saved_coffee_home, sizeof(saved_coffee_home), "%s", old);
	} else {
		saved_coffee_home[0] = '\0';
	}

	sds test_home = sdscatprintf(sdsempty(), "/tmp/coffee-config-%s", test_name);
	setenv("COFFEE_HOME", test_home, 1);

	mkdir(test_home, 0755);
	sdsfree(test_home);
}

static void restore_home(void)
{
	if (saved_coffee_home[0]) {
		setenv("COFFEE_HOME", saved_coffee_home, 1);
	} else {
		unsetenv("COFFEE_HOME");
	}
}

static sds test_config_path(const char *test_name)
{
	return sdscatprintf(sdsempty(), "/tmp/coffee-config-%s/config.toml", test_name);
}

/* Check if a line matching "key = \"...\"" exists in file */
static i64 file_has_key(const char *path, const char *key)
{
	FILE *fp = fopen(path, "r");
	if (fp == nullptr) {
		return 0;
	}
	char buf[4096];
	while (fgets(buf, sizeof(buf), fp)) {
		/* Look for `key =` anywhere in the line */
		if (strstr(buf, key) && strstr(buf, "=")) {
			fclose(fp);
			return 1;
		}
	}
	fclose(fp);
	return 0;
}

TEST(config_set_writes_file)
{
	set_test_home("set_writes_file");

	options opt_set = {
		.inputs     = (char *[]){ "coffee", "set", "testkey", "testvalue" },
		.inputs_num = 4,
	};
	i64 ret = handle_config(&opt_set);
	ASSERT(ret == 0, "config set should return 0");

	sds cfg_path = test_config_path("set_writes_file");
	i64 found    = file_has_key(cfg_path, "testkey");
	sdsfree(cfg_path);

	ASSERT(found, "config file should contain testkey after set");
	restore_home();
	PASS();
}

TEST(config_set_with_section)
{
	set_test_home("set_with_section");

	options opt_set = {
		.inputs     = (char *[]){ "coffee", "set", "registry.url", "https://example.com" },
		.inputs_num = 4,
	};
	i64 ret = handle_config(&opt_set);
	ASSERT(ret == 0, "config set with section should return 0");

	/* The file stores: [registry]\nurl = "..." */
	sds cfg_path = test_config_path("set_with_section");
	i64 found    = file_has_key(cfg_path, "url") && file_has_key(cfg_path, "example.com");
	sdsfree(cfg_path);

	ASSERT(found, "config file should contain the section key after set");
	restore_home();
	PASS();
}

TEST(config_set_multiple_keys)
{
	set_test_home("set_multiple_keys");

	options opt_a = {
		.inputs     = (char *[]){ "coffee", "set", "key_a", "val_a" },
		.inputs_num = 4,
	};
	ASSERT(handle_config(&opt_a) == 0, "set key_a");

	options opt_b = {
		.inputs     = (char *[]){ "coffee", "set", "key_b", "val_b" },
		.inputs_num = 4,
	};
	ASSERT(handle_config(&opt_b) == 0, "set key_b");

	sds cfg_path = test_config_path("set_multiple_keys");
	i64 has_a    = file_has_key(cfg_path, "key_a");
	i64 has_b    = file_has_key(cfg_path, "key_b");
	sdsfree(cfg_path);

	ASSERT(has_a, "should have key_a after second set");
	ASSERT(has_b, "should have key_b after second set");
	restore_home();
	PASS();
}

TEST(config_unset_removes_key)
{
	set_test_home("unset_removes_key");

	options opt_set = {
		.inputs     = (char *[]){ "coffee", "set", "unsettest", "willremove" },
		.inputs_num = 4,
	};
	ASSERT(handle_config(&opt_set) == 0, "config set should return 0");

	sds cfg_path = test_config_path("unset_removes_key");
	ASSERT(file_has_key(cfg_path, "unsettest"), "should find unsettest before unset");

	options opt_unset = {
		.inputs     = (char *[]){ "coffee", "unset", "unsettest" },
		.inputs_num = 3,
	};
	i64 ret = handle_config(&opt_unset);
	ASSERT(ret == 0, "config unset should return 0");

	i64 still_has = file_has_key(cfg_path, "unsettest");
	sdsfree(cfg_path);

	ASSERT(!still_has, "unsettest should be removed from file after unset");
	restore_home();
	PASS();
}

TEST(config_list_no_config)
{
	set_test_home("list_no_config");

	options opt_list = {
		.inputs     = (char *[]){ "coffee", "list" },
		.inputs_num = 2,
	};
	i64 ret = handle_config(&opt_list);
	ASSERT(ret == 0, "config list should return 0 even with no config");

	restore_home();
	PASS();
}

void coffee_register_config_tests(void)
{
	TEST_REGISTER(config_set_writes_file);
	TEST_REGISTER(config_set_with_section);
	TEST_REGISTER(config_set_multiple_keys);
	TEST_REGISTER(config_unset_removes_key);
	TEST_REGISTER(config_list_no_config);
}
