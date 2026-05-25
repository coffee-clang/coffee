/*
  Copyright (C) 2025 by the coffee developers
*/

#include "coffee.h"

command_s commands[] = {
	{.name = "add", .description = "Add dependencies to a manifest file", .action = &handle_add},
	{.name = "bench", .description = "Benchmark", .action = &handle_bench},
	{.name = "build", .description = "Compile a local package and all of its dependencies", .action = &handle_build},
	{.name = "b", .description = "alias: build", .action = &handle_build},
	{.name = "c", .description = "alias: check", .action = &handle_check},
	{.name		  = "cflags",
	 .description = "Output compiler flags for dependencies (pkg-config style)",
	 .action	  = &handle_cflags},
	{.name = "check", .description = "Analyze the current package and report errors", .action = &handle_check},
	{.name = "clean", .description = "Clean build artifacts", .action = &handle_clean},
	{.name		  = "compile",
	 .description = "Compile a local package and all of its dependencies",
	 .action	  = &handle_compile},
	{.name = "config", .description = "Inspect configuration values", .action = &handle_config},
	{.name = "doc", .description = "Build a package's documentation", .action = &handle_doc},
	{.name = "fetch", .description = "Fetch dependencies of a package from the network", .action = &handle_fetch},
	{.name = "fix", .description = "Automatically fix lint warnings", .action = &handle_fix},
	{.name = "fmt", .description = "Format all code in a package", .action = &handle_fmt},
	{.name		  = "generate-lockfile",
	 .description = "Generate the lockfile for the current project",
	 .action	  = &handle_generate_lockfile},
	{.name = "grep", .description = "Search for patterns in source code", .action = &handle_grep},
	{.name = "help", .description = "Display help information about coffee", .action = &handle_help},
	{.name = "info", .description = "Display information about a package", .action = &handle_info},
	{.name = "init", .description = "Create a new coffee package in an existing directory", .action = &handle_init},
	{.name		  = "install",
	 .description = "Install a Rust binary. Default location is $HOME/.cargo/bin",
	 .action	  = &handle_install},
	{.name		  = "install-update-config",
	 .description = "Install or update configuration as needed",
	 .action	  = &handle_install_update_config},
	{.name = "libs", .description = "Output linker flags for dependencies", .action = &handle_libs},
	{.name = "lint", .description = "Run the linter for a package", .action = &handle_lint},
	{.name		  = "locate-project",
	 .description = "Print the location of a project's manifest file",
	 .action	  = &handle_locate_project},
	{.name = "login", .description = "Save an API token for registry authentication", .action = &handle_login},
	{.name = "logout", .description = "Remove an API token for the registry locally", .action = &handle_logout},
	{.name = "machete", .description = "Detect unused dependencies", .action = &handle_machete},
	{.name		  = "metadata",
	 .description = "Output the resolved dependencies of a package in machine-readable format",
	 .action	  = &handle_metadata},
	{.name = "miri", .description = "Run Miri", .action = &handle_miri},
	{.name = "new", .description = "Create a new coffee package", .action = &handle_new},
	{.name = "owner", .description = "Manage the owners of a crate on the registry", .action = &handle_owner},
	{.name		  = "package",
	 .description = "Assemble the local package into a distributable archive",
	 .action	  = &handle_package},
	{.name		  = "pkgid",
	 .description = "Print a fully qualified package specification from a package ID spec",
	 .action	  = &handle_pkgid},
	{.name = "publish", .description = "Package and upload a package to the registry", .action = &handle_publish},
	{.name = "remove", .description = "Remove dependencies from a manifest file", .action = &handle_remove},
	{.name = "report", .description = "Generate and display various kinds of reports", .action = &handle_report},
	{.name = "rm", .description = "Remove a binary from the system", .action = &handle_rm},
	{.name = "run", .description = "Run a binary or example of the local package", .action = &handle_run},
	{.name = "search", .description = "Search packages in the registry", .action = &handle_search},
	{.name		  = "test",
	 .description = "Execute all unit and integration tests and build examples of a local package",
	 .action	  = &handle_test},
	{.name = "tree", .description = "Display a tree visualization of a dependency graph", .action = &handle_tree},
	{.name = "uninstall", .description = "Remove a Rust binary", .action = &handle_uninstall},
	{.name = "update", .description = "Update dependencies as recorded in the local", .action = &handle_update},
	{.name = "vendor", .description = "Vendor all dependencies for a project locally", .action = &handle_vendor},
	{.name = "version", .description = "Show version information", .action = &handle_version},
	{.name = "yank", .description = "Remove a pushed crate from the index", .action = &handle_yank},
	{.name = NULL, .description = NULL, .action = NULL},
};

#ifndef COFFEE_TEST_RUNNER

int main(int argc, char **argv)
{
	static struct gengetopt_args_info args_info;
	cmdline_parser(argc, argv, &args_info);

	// Find first non-toolchain argument (commands start with letter)
	int command_idx = -1;
	for (int i = 0; i < args_info.inputs_num; i++) {
		if (args_info.inputs[i][0] != '+') {
			command_idx = i;
			break;
		}
	}

	/* Process all options */
	options opt = {
		.offline	= args_info.offline_given,
		.locked		= args_info.locked_given,
		.verbose	= args_info.verbose_given,
		.verbose2	= args_info.verbose_given,
		.quiet		= args_info.quiet_given,
		.inputs		= args_info.inputs,
		.inputs_num = args_info.inputs_num,

		.pkg_version = args_info.pkg_version_given ? args_info.pkg_version_arg : NULL,
		.path		 = args_info.path_given ? args_info.path_arg : NULL,
		.git		 = args_info.git_given ? args_info.git_arg : NULL,
		.branch		 = args_info.branch_given ? args_info.branch_arg : NULL,
		.tag		 = args_info.tag_given ? args_info.tag_arg : NULL,
		.rev		 = args_info.rev_given ? args_info.rev_arg : NULL,
		.registry	 = args_info.registry_given ? args_info.registry_arg : NULL,
		.dev		 = args_info.dev_given,
		.build_dep	 = args_info.build_given,
		.optional	 = args_info.optional_given,

		.release			 = args_info.release_given,
		.debug				 = args_info.debug_given,
		.jobs				 = args_info.jobs_given ? args_info.jobs_arg : 0,
		.bin				 = args_info.bin_given ? args_info.bin_arg : NULL,
		.example			 = args_info.example_given ? args_info.example_arg : NULL,
		.features			 = args_info.features_given ? args_info.features_arg : NULL,
		.all_features		 = args_info.all_features_given,
		.no_default_features = args_info.no_default_features_given,
		.profile			 = args_info.profile_given ? args_info.profile_arg : NULL,
		.target				 = args_info.target_given ? args_info.target_arg : NULL,
		.target_dir			 = args_info.target_dir_given ? args_info.target_dir_arg : NULL,
		.manifest_path		 = args_info.manifest_path_given ? args_info.manifest_path_arg : NULL,
		.lib				 = args_info.lib_given,
		.fix				 = args_info.fix_given,
	};
	/* printf("command_idx: %d\n", command_idx); */
	/* printf("num commands: %d\n", lengthof(commands)); */
	/* Find the command, if it has been given */
	if (command_idx >= 0) {
		char *cmd = args_info.inputs[command_idx];
		for (i64 i = 0; i < lengthof(commands); i++) {
			if (strcmp(cmd, commands[i].name) == 0) {
				commands[i].action(&opt);
				exit(EXIT_SUCCESS);
			}
		}

		/* Check if we have received a command, but it's not in the list */
		printf("Command '%s' is not recognized.\n", cmd);
		cmdline_parser_print_help();
		exit(EXIT_FAILURE);
	}

	/* No command has been given */
	cmdline_parser_print_help();
	exit(EXIT_SUCCESS);
}

#endif /* COFFEE_TEST_RUNNER */