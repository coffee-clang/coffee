/*
  Copyright (C) 2025 by the coffee developers
*/

#include "coffee.h"

command_s commands[] = {
	{.name = "add", .description = "Add dependencies to a manifest file", .action = &handle_add},
	{.name = "bench", .description = "Benchmark", .action = &handle_bench},
	{.name	      = "build",
	 .description = "Compile a local package and all of its dependencies",
	 .action      = &handle_build},
	{.name = "b", .description = "alias: build", .action = &handle_build},
	{.name = "c", .description = "alias: check", .action = &handle_check},
	{.name = "check", .description = "Check a local package and all of its dependencies", .action = &handle_check},
	{.name = "clean", .description = "Remove generated artifacts", .action = &handle_clean},
	{.name = "compile", .description = "Compile", .action = &handle_compile},
	{.name = "config", .description = "Inspect configuration values", .action = &handle_config},
	{.name = "d", .description = "alias: doc", .action = &handle_doc},
	{.name = "doc", .description = "Build a package's documentation", .action = &handle_doc},
	{.name = "fetch", .description = "Fetch dependencies of a package from the network", .action = &handle_fetch},
	{.name	      = "fix",
	 .description = "Automatically fix lint warnings reported by the compiler",
	 .action      = &handle_fix},
	{.name	      = "fmt",
	 .description = "Formats all bin and lib files of the current crate using rustfmt.",
	 .action      = &handle_fmt},
	{.name	      = "generate-lockfile",
	 .description = "Generate the lockfile for a package",
	 .action      = &handle_generate_lockfile},
	{.name = "help", .description = "Displays help for a cargo subcommand", .action = &handle_help},
	{.name = "info", .description = "Display information about a package", .action = &handle_info},
	{.name = "init", .description = "Create a new cargo package in an existing", .action = &handle_init},
	{.name = "lint", .description = "Lint the package", .action = &handle_lint},
	{.name = "install", .description = "Install a binary", .action = &handle_install},
	{.name = "install-update", .description = "update", .action = &handle_install_update},
	{.name = "install-update-config", .description = "update-config", .action = &handle_install_update_config},
	{.name	      = "locate-project",
	 .description = "Print a JSON representation of a Cargo.toml",
	 .action      = &handle_locate_project},
	{.name = "login", .description = "Log in to a registry.", .action = &handle_login},
	{.name = "logout", .description = "Remove an API token from the registry locally", .action = &handle_logout},
	{.name = "machete", .description = "", .action = &handle_machete},
	{.name = "metadata", .description = "Output the resolved dependencies of a", .action = &handle_metadata},
	{.name	      = "package",
	 .description = "the concrete used versions including overrides, in machine-readable format",
	 .action      = &handle_package},
	{.name = "miri", .description = "", .action = &handle_miri},
	{.name = "new", .description = "Create a new package at <path>", .action = &handle_new},
	{.name = "owner", .description = "Manage the owners of a crate on the registry", .action = &handle_owner},
	{.name = "package", .description = "Assemble the local package into a", .action = &handle_package},
	{.name = "pkgid", .description = "Print a fully qualified package specification", .action = &handle_pkgid},
	{.name = "publish", .description = "Upload a package to the registry", .action = &handle_publish},
	{.name = "r", .description = "alias: run", .action = &handle_run},
	{.name = "remove", .description = "Remove dependencies from a manifest file", .action = &handle_remove},
	{.name = "report", .description = "Generate and display various kinds of reports", .action = &handle_report},
	{.name = "rm", .description = "alias: remove", .action = &handle_rm},
	{.name = "run", .description = "Run a binary or example of the local package", .action = &handle_run},
	{.name = "compile", .description = "Compile a package, and pass extra options to", .action = &handle_compile},
	{.name = "search", .description = "Search packages in the registry. Default", .action = &handle_search},
	{.name = "t", .description = "alias: test", .action = &handle_test},
	{.name = "test", .description = "Execute all unit and integration tests and", .action = &handle_test},
	{.name = "tree", .description = "Display a tree visualization of a dependency", .action = &handle_tree},
	{.name = "uninstall", .description = "Remove a Rust binary", .action = &handle_uninstall},
	{.name = "update", .description = "Update dependencies as recorded in the local", .action = &handle_update},
	{.name = "vendor", .description = "Vendor all dependencies for a project locally", .action = &handle_vendor},
	{.name = "version", .description = "Show version information", .action = &handle_version},
	{.name = "yank", .description = "Remove a pushed crate from the index", .action = &handle_yank},
};

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
		.offline    = args_info.offline_given,
		.locked	    = args_info.locked_given,
		.verbose    = args_info.verbose_given,
		.verbose2   = args_info.verbose_given,
		.quiet	    = args_info.quiet_given,
		.inputs	    = args_info.inputs,
		.inputs_num = args_info.inputs_num,
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
