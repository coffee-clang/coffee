/** @file cmdline.h
 *  @brief Command line option parser for coffee.
 *
 *  Hand-written replacement for the gengetopt-generated parser.
 *  Uses getopt_long directly with clean C23 style.
 */

#ifndef CMDLINE_H
#define CMDLINE_H

/* sds.h is vendored and not lint-clean; suppress its warnings */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wimplicit-int-conversion"
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#include <sds/sds.h>
#pragma GCC diagnostic pop

#include <stdbool.h>
#include <stdint.h>

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CMDLINE_PARSER_VERSION
# define CMDLINE_PARSER_VERSION "0.1.0"
#endif

/* Type aliases used throughout the project */
typedef uint64_t u64;
typedef int64_t  i64;

enum enum_toolchain {
	toolchain__NULL = -1,
	toolchain_arg_PLUS_stable,
	toolchain_arg_PLUS_clangMINUS_stable,
	toolchain_arg_PLUS_gccMINUS_stable
};

/** @brief Parsed command line options */
struct cli_args {
	/* Flag options */
	bool verbose_flag;
	bool quiet_flag;
	bool debug_flag;
	bool release_flag;
	bool all_features_flag;
	bool no_default_features_flag;
	bool unit_graph_flag;
	bool future_incompat_report_flag;
	bool workspace_flag;
	bool lib_flag;
	bool locked_flag;
	bool offline_flag;
	bool frozen_flag;
	bool dev_flag;
	bool build_flag;
	bool optional_flag;
	bool no_optional_flag;
	bool dry_run_flag;
	bool build_plan_flag;
	bool keep_going_flag;
	bool bins_flag;
	bool examples_flag;
	bool tests_flag;
	bool benches_flag;
	bool all_targets_flag;
	bool no_run_flag;
	bool no_fail_fast_flag;
	bool fix_flag;

	/* Integer options */
	i64 jobs_arg;

	/* String options */
	sds color_arg;
	sds message_format_arg;
	sds manifest_path_arg;
	sds target_arg;
	sds bin_arg;
	sds example_arg;
	sds features_arg;
	sds profile_arg;
	sds target_dir_arg;
	sds timings_arg;
	sds exclude_arg;
	sds include_arg;
	sds package_arg;
	sds config_arg;
	sds unstable_flags_arg;
	sds rename_arg;
	sds pkg_version_arg;
	sds path_arg;
	sds git_arg;
	sds branch_arg;
	sds tag_arg;
	sds rev_arg;
	sds registry_arg;
	sds out_dir_arg;
	sds command_arg;

	/* Enum options */
	enum enum_toolchain toolchain_arg;

	/* Whether each option was given */
	bool help_given;
	bool version_given;
	bool verbose_given;
	bool quiet_given;
	bool color_given;
	bool message_format_given;
	bool manifest_path_given;
	bool target_given;
	bool debug_given;
	bool release_given;
	bool jobs_given;
	bool bin_given;
	bool example_given;
	bool features_given;
	bool all_features_given;
	bool no_default_features_given;
	bool profile_given;
	bool target_dir_given;
	bool unit_graph_given;
	bool timings_given;
	bool future_incompat_report_given;
	bool workspace_given;
	bool exclude_given;
	bool include_given;
	bool lib_given;
	bool package_given;
	bool locked_given;
	bool offline_given;
	bool frozen_given;
	bool config_given;
	bool unstable_flags_given;
	bool dev_given;
	bool build_given;
	bool optional_given;
	bool no_optional_given;
	bool rename_given;
	bool pkg_version_given;
	bool path_given;
	bool git_given;
	bool branch_given;
	bool tag_given;
	bool rev_given;
	bool registry_given;
	bool dry_run_given;
	bool out_dir_given;
	bool build_plan_given;
	bool keep_going_given;
	bool bins_given;
	bool examples_given;
	bool tests_given;
	bool benches_given;
	bool all_targets_given;
	bool no_run_given;
	bool no_fail_fast_given;
	bool fix_given;
	bool toolchain_given;
	bool command_given;

	/* Unnamed (positional) arguments */
	sds     *inputs;
	unsigned inputs_num;
};

/** @brief Possible values for toolchain. */
extern const char *cmdline_parser_toolchain_values[];

/**
 * Parse command line options.
 * @param argc argument count
 * @param argv argument vector
 * @param args_info output structure
 * @return 0 on success, non-zero on error
 */
[[nodiscard]]
i64 cmdline_parser(i64 argc, char **argv, struct cli_args *args_info);

/**
 * Initialize the args_info structure to defaults.
 */
void cmdline_parser_init(struct cli_args *args_info);

/**
 * Free allocated memory inside args_info.
 */
void cmdline_parser_free(struct cli_args *args_info);

/**
 * Print help text to stdout.
 */
void cmdline_parser_print_help(void);

/**
 * Print version information to stdout.
 */
void cmdline_parser_print_version(void);

#ifdef __cplusplus
}
#endif

#endif /* CMDLINE_H */
