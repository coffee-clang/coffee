#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cmdline.h"

void print_usage() {
    cmdline_parser_print_help();
}

int main(int argc, char *argv[]) {
    struct gengetopt_args_info args_info;
    int parse_result = cmdline_parser(argc, argv, &args_info);
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
    char *command = args_info.inputs[0];
    const char *allowed_commands[] = {"build", "run", "test", "check", "doc", "bench", "new", "init", "publish", "install", "update", "search"};
    int allowed = 0;
    int allowed_count = sizeof(allowed_commands) / sizeof(allowed_commands[0]);
    for (int i = 0; i < allowed_count; i++) {
        if (strcmp(command, allowed_commands[i]) == 0) {
            allowed = 1;
            break;
        }
    }
    if (!allowed) {
        fprintf(stderr, "Unknown command: %s\n", command);
        print_usage();
        cmdline_parser_free(&args_info);
        exit(1);
    }

    // Build a substring from the command and all unnamed options (values)
    int total_length = 0;
    for (int i = 0; i < args_info.inputs_num; i++) {
        total_length += strlen(args_info.inputs[i]);
        if (i < args_info.inputs_num - 1) {
            total_length += 1; // for space
        }
    }
    total_length += 1; // for null terminator

    char *cmd_line_substring = (char *)malloc(total_length * sizeof(char));
    if (cmd_line_substring == NULL) {
        fprintf(stderr, "Memory allocation error\n");
        cmdline_parser_free(&args_info);
        exit(1);
    }
    cmd_line_substring[0] = '\0';

    for (int i = 0; i < args_info.inputs_num; i++) {
        strcat(cmd_line_substring, args_info.inputs[i]);
        if (i < args_info.inputs_num - 1) {
            strcat(cmd_line_substring, " ");
        }
    }

    // Print the substring of the command line with the command and its unnamed options
    printf("Command line substring: %s\n", cmd_line_substring);

    // Simulate execution of the cargo command
    printf("Executing cargo %s command...\n", command);

    free(cmd_line_substring);
    cmdline_parser_free(&args_info);
    return 0;
}
