#include "cmdline.h"
#include "strings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_usage()
{
	cmdline_parser_print_help();
}

int main(int argc, char *argv[])
{
	struct gengetopt_args_info args_info;
	int                        parse_result = cmdline_parser(argc, argv, &args_info);
	if (parse_result != 0) {
		// Error messages are printed by the generated parser.
		exit(1);
	}
	// Ensure there is at least one unnamed argument (the command)
	if (args_info.inputs_num < 1) {
		print_usage();
		exit(1);
	}
	// The first unnamed argument is treated as the command
	char       *command            = args_info.inputs[0];
	const char *allowed_commands[] = { "build", "run",  "test",    "check",   "doc",    "bench",
		                               "new",   "init", "publish", "install", "update", "search" };
	int         allowed            = 0;
	int         allowed_count      = sizeof(allowed_commands) / sizeof(allowed_commands[0]);
	for (int i = 0; i < allowed_count; i++) {
		if (strcmp(command, allowed_commands[i]) == 0) {
			allowed = 1;
			break;
		}
	}
	if (!allowed) {
		fprintf_safe(stderr, "Unknown command: %s\n", command);
		print_usage();
		cmdline_parser_free(&args_info);
		exit(1);
	}
	// Build a substring from the command and all unnamed options (values)
	size_t total_length = 0;
	for (int i = 0; i < args_info.inputs_num; i++) {
		total_length += strlen(args_info.inputs[i]);
		if (i < args_info.inputs_num - 1) {
			total_length += 1; // for space
		}
	}
	total_length += 1; // for null terminator

	sds cmd_line_substring = sdsempty();
	if (cmd_line_substring == nullptr) {
		fprintf_safe(stderr, "Memory allocation error\n");
		cmdline_parser_free(&args_info);
		exit(1);
	}

	for (int i = 0; i < args_info.inputs_num; i++) {
		cmd_line_substring = sdscat(cmd_line_substring, args_info.inputs[i]);
		if (i < args_info.inputs_num - 1) {
			cmd_line_substring = sdscat(cmd_line_substring, " ");
		}
	}

	// Print the substring of the command line with the command and its unnamed options
	printf("Command line substring: %s\n", cmd_line_substring);

	// Simulate execution of the cargo command
	printf("Executing cargo %s command...\n", command);

	sdsfree(cmd_line_substring);
	cmdline_parser_free(&args_info);
	return 0;
}
