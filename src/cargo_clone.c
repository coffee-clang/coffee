#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_usage() {
    printf("Usage: cargo_clone <command> [options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  build, run, test, check, doc, bench, new, init, publish, install, update, search\n");
    printf("\n");
    printf("Global Options:\n");
    printf("  --help, -h                Show this help information\n");
    printf("  --version, -V             Show version information\n");
    printf("\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    // Check for global options in the first argument
    char *arg1 = argv[1];
    if (strcmp(arg1, "--help") == 0 || strcmp(arg1, "-h") == 0) {
        print_usage();
        return 0;
    }
    if (strcmp(arg1, "--version") == 0 || strcmp(arg1, "-V") == 0) {
        printf("cargo_clone version 1.0\n");
        return 0;
    }

    // Treat the first argument as the command
    char *command = argv[1];
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
        printf("Unknown command: %s\n", command);
        print_usage();
        return 1;
    }

    // Build a substring from command and all options (arguments 1 through argc-1)
    int total_length = 0;
    for (int i = 1; i < argc; i++) {
        total_length += strlen(argv[i]);
        if (i < argc - 1) {
            total_length += 1; // for space
        }
    }
    total_length += 1; // for null terminator

    char *cmd_line_substring = (char *)malloc(total_length * sizeof(char));
    if (cmd_line_substring == NULL) {
        fprintf(stderr, "Memory allocation error\n");
        return 1;
    }
    cmd_line_substring[0] = '\0';

    for (int i = 1; i < argc; i++) {
        strcat(cmd_line_substring, argv[i]);
        if (i < argc - 1) {
            strcat(cmd_line_substring, " ");
        }
    }

    // Print the substring of the command line including the command and all options
    printf("Command line substring: %s\n", cmd_line_substring);

    // Simulate execution of the cargo command
    printf("Executing cargo %s command...\n", command);

    free(cmd_line_substring);
    return 0;
}
