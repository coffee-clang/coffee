/** @file cmdline.c
 *  @brief Command line option parser for coffee.
 *
 *  Hand-written replacement for the gengetopt-generated parser.
 *  Uses getopt_long directly with clean C23 style.
 */

#include "cmdline.h"

#include "strings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <getopt.h>
#include <sds/sds.h>

/* ------------------------------------------------------------------ */
/*  Toolchain values                                                  */
/* ------------------------------------------------------------------ */

const char *cmdline_parser_toolchain_values[] = { "+stable", "+clang-stable", "+gcc-stable", nullptr };

/* ------------------------------------------------------------------ */
/*  Help / usage text                                                 */
/* ------------------------------------------------------------------ */

static const char *usage_str = "Usage: coffee [OPTIONS]... [COMMAND]...\n"
                               "\n"
                               "A modern package manager for C\n"
                               "\n"
                               "  -h, --help                    Print help and exit\n"
                               "      --version                 Print version and exit\n"
                               "  -v, --verbose                 Use verbose output (-vv very verbose) (default=off)\n"
                               "  -q, --quiet                   Do not print coffee log messages (default=off)\n"
                               "      --color=color             Coloring [possible values: auto, always, never]\n"
                               "      --message-format=fmt      Error format [possible values: human, json, short]\n"
                               "      --manifest-path=path      Path to Coffee.toml\n"
                               "      --target=target           Build for the target triple\n"
                               "      --debug                   Build with debug symbols (default=off)\n"
                               "      --release                 Build in release mode (default=off)\n"
                               "  -j, --jobs=N                  Number of parallel jobs, defaults to # of CPUs\n"
                               "      --bin=name                Build only the specified binary\n"
                               "      --example=name            Build only the specified example\n"
                               "      --features=features       Space-separated list of features to activate\n"
                               "      --all-features            Activate all available features (default=off)\n"
                               "      --no-default-features     Do not activate the `default` feature (default=off)\n"
                               "      --profile=profile         Build with given profile\n"
                               "      --target-dir=dir          Directory for all generated artifacts\n"
                               "      --unit-graph              Output build graph in JSON (default=off)\n"
                               "      --timings=timings         Output build timing information\n"
                               "      --future-incompat-report  Output future incompatibility report (default=off)\n"
                               "      --workspace               Build all packages in the workspace (default=off)\n"
                               "      --exclude=exclude         Exclude packages from the build\n"
                               "      --include=include         Include packages in the build\n"
                               "      --lib                     Build only this package's library (default=off)\n"
                               "  -p, --package=pkgid           Package to build\n"
                               "      --locked                  Require Coffee.lock is up to date (default=off)\n"
                               "      --offline                 Run without accessing the network (default=off)\n"
                               "      --frozen                  Equivalent to --locked and --offline (default=off)\n"
                               "      --config=config           Override a configuration value\n"
                               "  -Z, --unstable-flags=flags    Unstable (nightly-only) flags\n"
                               "      --dev                     Add as a development dependency (default=off)\n"
                               "      --build                   Add as a build dependency (default=off)\n"
                               "      --optional                Mark dependency as optional (default=off)\n"
                               "      --no-optional             Mark dependency as required (default=off)\n"
                               "      --rename=name             Rename the dependency\n"
                               "  -V, --pkg-version=version     Version constraint\n"
                               "      --path=path               Filesystem path to local dependency\n"
                               "      --git=url                 Git repository location\n"
                               "      --branch=branch           Git branch to download\n"
                               "      --tag=tag                 Git tag to download\n"
                               "      --rev=rev                 Git commit reference to download\n"
                               "      --registry=registry       Package registry for this dependency\n"
                               "      --dry-run                 Don't actually write the manifest (default=off)\n"
                               "      --out-dir=dir             Output directory for artifacts\n"
                               "      --build-plan              Output the build plan in JSON (default=off)\n"
                               "      --keep-going              Continue building as much as possible (default=off)\n"
                               "      --bins                    Build all binary targets (default=off)\n"
                               "      --examples                Build all example targets (default=off)\n"
                               "      --tests                   Build all test targets (default=off)\n"
                               "      --benches                 Build all bench targets (default=off)\n"
                               "      --all-targets             Build all targets (default=off)\n"
                               "      --no-run                  Don't run the generated binaries (default=off)\n"
                               "      --no-fail-fast            Run all tests regardless of failure (default=off)\n"
                               "      --fix                     Fix warnings automatically (default=off)\n";

/* ------------------------------------------------------------------ */
/*  Forward declarations                                              */
/* ------------------------------------------------------------------ */

static i64 check_enum_value(const char *val, const char *values[], i64 *out_index);

/* ------------------------------------------------------------------ */
/*  cmdline_parser_init                                               */
/* ------------------------------------------------------------------ */

void cmdline_parser_init(struct cli_args *args_info)
{
	if (args_info == nullptr) {
		return;
	}

	/* Zero everything */
	memset(args_info, 0, sizeof(*args_info));

	/* Set defaults for enums */
	args_info->toolchain_arg = toolchain__NULL;
}

/* ------------------------------------------------------------------ */
/*  cmdline_parser_free                                               */
/* ------------------------------------------------------------------ */

void cmdline_parser_free(struct cli_args *args_info)
{
	if (args_info == nullptr) {
		return;
	}

	sdsfree(args_info->color_arg);
	sdsfree(args_info->message_format_arg);
	sdsfree(args_info->manifest_path_arg);
	sdsfree(args_info->target_arg);
	sdsfree(args_info->bin_arg);
	sdsfree(args_info->example_arg);
	sdsfree(args_info->features_arg);
	sdsfree(args_info->profile_arg);
	sdsfree(args_info->target_dir_arg);
	sdsfree(args_info->timings_arg);
	sdsfree(args_info->exclude_arg);
	sdsfree(args_info->include_arg);
	sdsfree(args_info->package_arg);
	sdsfree(args_info->config_arg);
	sdsfree(args_info->unstable_flags_arg);
	sdsfree(args_info->rename_arg);
	sdsfree(args_info->pkg_version_arg);
	sdsfree(args_info->path_arg);
	sdsfree(args_info->git_arg);
	sdsfree(args_info->branch_arg);
	sdsfree(args_info->tag_arg);
	sdsfree(args_info->rev_arg);
	sdsfree(args_info->registry_arg);
	sdsfree(args_info->out_dir_arg);
	sdsfree(args_info->command_arg);

	/* Free positional arguments */
	for (unsigned i = 0; i < args_info->inputs_num; i++) {
		sdsfree(args_info->inputs[i]);
	}
	free(args_info->inputs);
	args_info->inputs     = nullptr;
	args_info->inputs_num = 0;

	/* Reset to defaults */
	cmdline_parser_init(args_info);
}

/* ------------------------------------------------------------------ */
/*  check_enum_value — match a string against a list of values        */
/*  Returns 0 on success, -1 on invalid, -2 on ambiguous.             */
/* ------------------------------------------------------------------ */

static i64 check_enum_value(const char *val, const char *values[], i64 *out_index)
{
	if (val == nullptr || !values) {
		return -1;
	}

	i64    found  = -1;
	size_t vallen = strlen(val);

	for (i64 i = 0; values[i]; i++) {
		if (strncmp(val, values[i], vallen) == 0) {
			if (strlen(values[i]) == vallen) {
				/* Exact match */
				*out_index = i;
				return 0;
			}
			if (found >= 0) {
				return -2; /* ambiguous */
			}
			found = i;
		}
	}

	if (found >= 0) {
		*out_index = found;
		return 0;
	}

	return -1; /* not found */
}

/* ------------------------------------------------------------------ */
/*  cmdline_parser_print_help / _version                              */
/* ------------------------------------------------------------------ */

void cmdline_parser_print_version(void)
{
	printf_safe("coffee %s\n", CMDLINE_PARSER_VERSION);
}

void cmdline_parser_print_help(void)
{
	printf_safe("%s", usage_str);
}

/* ------------------------------------------------------------------ */
/*  Long option descriptor                                            */
/* ------------------------------------------------------------------ */

enum long_opt_id {
	LOPT_VERSION = 256,
	LOPT_COLOR,
	LOPT_MESSAGE_FORMAT,
	LOPT_MANIFEST_PATH,
	LOPT_TARGET,
	LOPT_DEBUG,
	LOPT_RELEASE,
	LOPT_BIN,
	LOPT_EXAMPLE,
	LOPT_FEATURES,
	LOPT_ALL_FEATURES,
	LOPT_NO_DEFAULT_FEATURES,
	LOPT_PROFILE,
	LOPT_TARGET_DIR,
	LOPT_UNIT_GRAPH,
	LOPT_TIMINGS,
	LOPT_FUTURE_INCOMPAT_REPORT,
	LOPT_WORKSPACE,
	LOPT_EXCLUDE,
	LOPT_INCLUDE,
	LOPT_LIB,
	LOPT_LOCKED,
	LOPT_OFFLINE,
	LOPT_FROZEN,
	LOPT_CONFIG,
	LOPT_DEV,
	LOPT_BUILD,
	LOPT_OPTIONAL,
	LOPT_NO_OPTIONAL,
	LOPT_RENAME,
	LOPT_PATH,
	LOPT_GIT,
	LOPT_BRANCH,
	LOPT_TAG,
	LOPT_REV,
	LOPT_REGISTRY,
	LOPT_DRY_RUN,
	LOPT_OUT_DIR,
	LOPT_BUILD_PLAN,
	LOPT_KEEP_GOING,
	LOPT_BINS,
	LOPT_EXAMPLES,
	LOPT_TESTS,
	LOPT_BENCHES,
	LOPT_ALL_TARGETS,
	LOPT_NO_RUN,
	LOPT_NO_FAIL_FAST,
	LOPT_FIX,
	LOPT_TOOLCHAIN,
	LOPT_COMMAND,
};

static const struct option long_options[] = { { "help", no_argument, 0, 'h' },
	                                          { "version", no_argument, 0, LOPT_VERSION },
	                                          { "verbose", no_argument, 0, 'v' },
	                                          { "quiet", no_argument, 0, 'q' },
	                                          { "color", required_argument, 0, LOPT_COLOR },
	                                          { "message-format", required_argument, 0, LOPT_MESSAGE_FORMAT },
	                                          { "manifest-path", required_argument, 0, LOPT_MANIFEST_PATH },
	                                          { "target", required_argument, 0, LOPT_TARGET },
	                                          { "debug", no_argument, 0, LOPT_DEBUG },
	                                          { "release", no_argument, 0, LOPT_RELEASE },
	                                          { "jobs", required_argument, 0, 'j' },
	                                          { "bin", required_argument, 0, LOPT_BIN },
	                                          { "example", required_argument, 0, LOPT_EXAMPLE },
	                                          { "features", required_argument, 0, LOPT_FEATURES },
	                                          { "all-features", no_argument, 0, LOPT_ALL_FEATURES },
	                                          { "no-default-features", no_argument, 0, LOPT_NO_DEFAULT_FEATURES },
	                                          { "profile", required_argument, 0, LOPT_PROFILE },
	                                          { "target-dir", required_argument, 0, LOPT_TARGET_DIR },
	                                          { "unit-graph", no_argument, 0, LOPT_UNIT_GRAPH },
	                                          { "timings", required_argument, 0, LOPT_TIMINGS },
	                                          { "future-incompat-report", no_argument, 0, LOPT_FUTURE_INCOMPAT_REPORT },
	                                          { "workspace", no_argument, 0, LOPT_WORKSPACE },
	                                          { "exclude", required_argument, 0, LOPT_EXCLUDE },
	                                          { "include", required_argument, 0, LOPT_INCLUDE },
	                                          { "lib", no_argument, 0, LOPT_LIB },
	                                          { "package", required_argument, 0, 'p' },
	                                          { "locked", no_argument, 0, LOPT_LOCKED },
	                                          { "offline", no_argument, 0, LOPT_OFFLINE },
	                                          { "frozen", no_argument, 0, LOPT_FROZEN },
	                                          { "config", required_argument, 0, LOPT_CONFIG },
	                                          { "unstable-flags", required_argument, 0, 'Z' },
	                                          { "dev", no_argument, 0, LOPT_DEV },
	                                          { "build", no_argument, 0, LOPT_BUILD },
	                                          { "optional", no_argument, 0, LOPT_OPTIONAL },
	                                          { "no-optional", no_argument, 0, LOPT_NO_OPTIONAL },
	                                          { "rename", required_argument, 0, LOPT_RENAME },
	                                          { "pkg-version", required_argument, 0, 'V' },
	                                          { "path", required_argument, 0, LOPT_PATH },
	                                          { "git", required_argument, 0, LOPT_GIT },
	                                          { "branch", required_argument, 0, LOPT_BRANCH },
	                                          { "tag", required_argument, 0, LOPT_TAG },
	                                          { "rev", required_argument, 0, LOPT_REV },
	                                          { "registry", required_argument, 0, LOPT_REGISTRY },
	                                          { "dry-run", no_argument, 0, LOPT_DRY_RUN },
	                                          { "out-dir", required_argument, 0, LOPT_OUT_DIR },
	                                          { "build-plan", no_argument, 0, LOPT_BUILD_PLAN },
	                                          { "keep-going", no_argument, 0, LOPT_KEEP_GOING },
	                                          { "bins", no_argument, 0, LOPT_BINS },
	                                          { "examples", no_argument, 0, LOPT_EXAMPLES },
	                                          { "tests", no_argument, 0, LOPT_TESTS },
	                                          { "benches", no_argument, 0, LOPT_BENCHES },
	                                          { "all-targets", no_argument, 0, LOPT_ALL_TARGETS },
	                                          { "no-run", no_argument, 0, LOPT_NO_RUN },
	                                          { "no-fail-fast", no_argument, 0, LOPT_NO_FAIL_FAST },
	                                          { "fix", no_argument, 0, LOPT_FIX },
	                                          { "toolchain", required_argument, 0, LOPT_TOOLCHAIN },
	                                          { "command", required_argument, 0, LOPT_COMMAND },
	                                          { 0, 0, 0, 0 } };

/* ------------------------------------------------------------------ */
/*  cmdline_parser — main entry point                                 */
/* ------------------------------------------------------------------ */

i64 cmdline_parser(i64 argc, char **argv, struct cli_args *args_info)
{
	if (args_info == nullptr) {
		return 1;
	}

	cmdline_parser_init(args_info);

	optind = 1;
	i64 ch;
	while ((ch = getopt_long((int)argc, argv, "hvqj:p:Z:V:", long_options, nullptr)) != -1) {
		switch (ch) {
		case 'h':
			args_info->help_given = true;
			cmdline_parser_print_help();
			cmdline_parser_free(args_info);
			exit(EXIT_SUCCESS);

		case LOPT_VERSION:
			args_info->version_given = true;
			cmdline_parser_print_version();
			cmdline_parser_free(args_info);
			exit(EXIT_SUCCESS);

		case 'v':
			args_info->verbose_given = true;
			args_info->verbose_flag  = (bool)(!args_info->verbose_flag);
			break;

		case 'q':
			args_info->quiet_given = true;
			args_info->quiet_flag  = true;
			break;

		case 'j':
			args_info->jobs_given = true;
			{
				char *end;
				long  val = strtol(optarg, &end, 10);
				if (end == optarg || *end != '\0' || val < 0 || val > 2147483647) {
					fprintf_safe(stderr, "coffee: invalid numeric value: %s\n", optarg);
					cmdline_parser_free(args_info);
					return 1;
				}
				args_info->jobs_arg = (i64)val;
			}
			break;

		case 'p':
			args_info->package_given = true;
			sdsfree(args_info->package_arg);
			args_info->package_arg = sdsnew(optarg);
			if (!args_info->package_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case 'Z':
			args_info->unstable_flags_given = true;
			sdsfree(args_info->unstable_flags_arg);
			args_info->unstable_flags_arg = sdsnew(optarg);
			if (!args_info->unstable_flags_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case 'V':
			args_info->pkg_version_given = true;
			sdsfree(args_info->pkg_version_arg);
			args_info->pkg_version_arg = sdsnew(optarg);
			if (!args_info->pkg_version_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		/* --- Long options without short form --- */
		case LOPT_COLOR:
			args_info->color_given = true;
			sdsfree(args_info->color_arg);
			args_info->color_arg = sdsnew(optarg);
			if (!args_info->color_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case LOPT_MESSAGE_FORMAT:
			args_info->message_format_given = true;
			sdsfree(args_info->message_format_arg);
			args_info->message_format_arg = sdsnew(optarg);
			if (!args_info->message_format_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case LOPT_MANIFEST_PATH:
			args_info->manifest_path_given = true;
			sdsfree(args_info->manifest_path_arg);
			args_info->manifest_path_arg = sdsnew(optarg);
			if (!args_info->manifest_path_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case LOPT_TARGET:
			args_info->target_given = true;
			sdsfree(args_info->target_arg);
			args_info->target_arg = sdsnew(optarg);
			if (!args_info->target_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case LOPT_DEBUG:
			args_info->debug_given = true;
			args_info->debug_flag  = true;
			break;

		case LOPT_RELEASE:
			args_info->release_given = true;
			args_info->release_flag  = true;
			break;

		case LOPT_BIN:
			args_info->bin_given = true;
			sdsfree(args_info->bin_arg);
			args_info->bin_arg = sdsnew(optarg);
			if (!args_info->bin_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case LOPT_EXAMPLE:
			args_info->example_given = true;
			sdsfree(args_info->example_arg);
			args_info->example_arg = sdsnew(optarg);
			if (!args_info->example_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case LOPT_FEATURES:
			args_info->features_given = true;
			sdsfree(args_info->features_arg);
			args_info->features_arg = sdsnew(optarg);
			if (!args_info->features_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case LOPT_ALL_FEATURES:
			args_info->all_features_given = true;
			args_info->all_features_flag  = true;
			break;

		case LOPT_NO_DEFAULT_FEATURES:
			args_info->no_default_features_given = true;
			args_info->no_default_features_flag  = true;
			break;

		case LOPT_PROFILE:
			args_info->profile_given = true;
			sdsfree(args_info->profile_arg);
			args_info->profile_arg = sdsnew(optarg);
			if (!args_info->profile_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case LOPT_TARGET_DIR:
			args_info->target_dir_given = true;
			sdsfree(args_info->target_dir_arg);
			args_info->target_dir_arg = sdsnew(optarg);
			if (!args_info->target_dir_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case LOPT_UNIT_GRAPH:
			args_info->unit_graph_given = true;
			args_info->unit_graph_flag  = true;
			break;

		case LOPT_TIMINGS:
			args_info->timings_given = true;
			sdsfree(args_info->timings_arg);
			args_info->timings_arg = sdsnew(optarg);
			if (!args_info->timings_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case LOPT_FUTURE_INCOMPAT_REPORT:
			args_info->future_incompat_report_given = true;
			args_info->future_incompat_report_flag  = true;
			break;

		case LOPT_WORKSPACE:
			args_info->workspace_given = true;
			args_info->workspace_flag  = true;
			break;

		case LOPT_EXCLUDE:
			args_info->exclude_given = true;
			sdsfree(args_info->exclude_arg);
			args_info->exclude_arg = sdsnew(optarg);
			if (!args_info->exclude_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case LOPT_INCLUDE:
			args_info->include_given = true;
			sdsfree(args_info->include_arg);
			args_info->include_arg = sdsnew(optarg);
			if (!args_info->include_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case LOPT_LIB:
			args_info->lib_given = true;
			args_info->lib_flag  = true;
			break;

		case LOPT_LOCKED:
			args_info->locked_given = true;
			args_info->locked_flag  = true;
			break;

		case LOPT_OFFLINE:
			args_info->offline_given = true;
			args_info->offline_flag  = true;
			break;

		case LOPT_FROZEN:
			args_info->frozen_given = true;
			args_info->frozen_flag  = true;
			break;

		case LOPT_CONFIG:
			args_info->config_given = true;
			sdsfree(args_info->config_arg);
			args_info->config_arg = sdsnew(optarg);
			if (!args_info->config_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case LOPT_DEV:
			args_info->dev_given = true;
			args_info->dev_flag  = true;
			break;

		case LOPT_BUILD:
			args_info->build_given = true;
			args_info->build_flag  = true;
			break;

		case LOPT_OPTIONAL:
			args_info->optional_given = true;
			args_info->optional_flag  = true;
			break;

		case LOPT_NO_OPTIONAL:
			args_info->no_optional_given = true;
			args_info->no_optional_flag  = true;
			break;

		case LOPT_RENAME:
			args_info->rename_given = true;
			sdsfree(args_info->rename_arg);
			args_info->rename_arg = sdsnew(optarg);
			if (!args_info->rename_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case LOPT_PATH:
			args_info->path_given = true;
			sdsfree(args_info->path_arg);
			args_info->path_arg = sdsnew(optarg);
			if (!args_info->path_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case LOPT_GIT:
			args_info->git_given = true;
			sdsfree(args_info->git_arg);
			args_info->git_arg = sdsnew(optarg);
			if (!args_info->git_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case LOPT_BRANCH:
			args_info->branch_given = true;
			sdsfree(args_info->branch_arg);
			args_info->branch_arg = sdsnew(optarg);
			if (!args_info->branch_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case LOPT_TAG:
			args_info->tag_given = true;
			sdsfree(args_info->tag_arg);
			args_info->tag_arg = sdsnew(optarg);
			if (!args_info->tag_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case LOPT_REV:
			args_info->rev_given = true;
			sdsfree(args_info->rev_arg);
			args_info->rev_arg = sdsnew(optarg);
			if (!args_info->rev_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case LOPT_REGISTRY:
			args_info->registry_given = true;
			sdsfree(args_info->registry_arg);
			args_info->registry_arg = sdsnew(optarg);
			if (!args_info->registry_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case LOPT_DRY_RUN:
			args_info->dry_run_given = true;
			args_info->dry_run_flag  = true;
			break;

		case LOPT_OUT_DIR:
			args_info->out_dir_given = true;
			sdsfree(args_info->out_dir_arg);
			args_info->out_dir_arg = sdsnew(optarg);
			if (!args_info->out_dir_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case LOPT_BUILD_PLAN:
			args_info->build_plan_given = true;
			args_info->build_plan_flag  = true;
			break;

		case LOPT_KEEP_GOING:
			args_info->keep_going_given = true;
			args_info->keep_going_flag  = true;
			break;

		case LOPT_BINS:
			args_info->bins_given = true;
			args_info->bins_flag  = true;
			break;

		case LOPT_EXAMPLES:
			args_info->examples_given = true;
			args_info->examples_flag  = true;
			break;

		case LOPT_TESTS:
			args_info->tests_given = true;
			args_info->tests_flag  = true;
			break;

		case LOPT_BENCHES:
			args_info->benches_given = true;
			args_info->benches_flag  = true;
			break;

		case LOPT_ALL_TARGETS:
			args_info->all_targets_given = true;
			args_info->all_targets_flag  = true;
			break;

		case LOPT_NO_RUN:
			args_info->no_run_given = true;
			args_info->no_run_flag  = true;
			break;

		case LOPT_NO_FAIL_FAST:
			args_info->no_fail_fast_given = true;
			args_info->no_fail_fast_flag  = true;
			break;

		case LOPT_FIX:
			args_info->fix_given = true;
			args_info->fix_flag  = true;
			break;

		case LOPT_TOOLCHAIN:
			args_info->toolchain_given = true;
			{
				i64 idx;
				i64 rc = check_enum_value(optarg, cmdline_parser_toolchain_values, &idx);
				if (rc == -2) {
					fprintf_safe(stderr, "coffee: ambiguous argument \"%s\" for --toolchain\n", optarg);
					cmdline_parser_free(args_info);
					return 1;
				}
				if (rc == -1) {
					fprintf_safe(stderr, "coffee: invalid argument \"%s\" for --toolchain\n", optarg);
					cmdline_parser_free(args_info);
					return 1;
				}
				args_info->toolchain_arg = (enum enum_toolchain)idx;
			}
			break;

		case LOPT_COMMAND:
			args_info->command_given = true;
			sdsfree(args_info->command_arg);
			args_info->command_arg = sdsnew(optarg);
			if (!args_info->command_arg) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
			break;

		case '?':
			/* getopt_long already printed an error */
			cmdline_parser_free(args_info);
			return 1;

		default:
			fprintf_safe(stderr, "coffee: unknown option (bug)\n");
			cmdline_parser_free(args_info);
			return 1;
		}
	}

	/* Collect positional arguments */
	if (optind < argc) {
		unsigned remaining = (unsigned)(argc - optind);
		args_info->inputs  = (sds *)malloc((size_t)remaining * sizeof(sds));
		if (!args_info->inputs) {
			fprintf_safe(stderr, "coffee: out of memory\n");
			cmdline_parser_free(args_info);
			return 1;
		}
		for (unsigned i = 0; i < remaining; i++) {
			args_info->inputs[i] = sdsnew(argv[optind + (i64)i]);
			if (!args_info->inputs[i]) {
				fprintf_safe(stderr, "coffee: out of memory\n");
				cmdline_parser_free(args_info);
				return 1;
			}
		}
		args_info->inputs_num = remaining;
	}

	return 0;
}
