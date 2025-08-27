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

    // Print the command
    printf("Command: %s\n", command);

    // Check for any additional arguments (not parsed as options)
    if (argc > 2) {
        printf("Additional arguments:\n");
        for (int i = 2; i < argc; i++) {
            printf("  %s\n", argv[i]);
        }
    }

    // Simulate execution of the cargo command
    printf("Executing cargo %s command...\n", command);

    return 0;
}
