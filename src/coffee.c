/*
  Copyright (C) 2025 by the coffee developers
*/

#include "coffee.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int64_t handle_add(options *) {
    printf("Handling command add\n");
    return 0;
}

static int64_t handle_build(options *) {
    printf("Handling command build\n");
    return 0;
}

/* static int64_t handle_bench(options *) { printf("Handling command bench\n"); } */

/* static int64_t handle_check(options *) { printf("Handling command check\n"); } */

/* static int64_t handle_clean(options *) { printf("Handling command clean\n"); } */

/* static int64_t handle_config(options *) { printf("Handling command config\n"); } */

/* static int64_t handle_doc(options *) { printf("Handling command doc\n"); } */

/* static int64_t handle_lint(options *) { printf("Handling command lint\n"); } */

/* static int64_t handle_fetch(options *) { printf("Handling command fetch\n"); } */

/* static int64_t handle_fix(options *) { printf("Handling command fix\n"); } */

/* static int64_t handle_fmt(options *) { printf("Handling command fmt\n"); } */

/* static int64_t handle_generate_lockfile(options *) { */
/*     printf("Handling command generate-lockfile\n"); */
/* } */

/* static int64_t handle_help(options *) { printf("Handling command help\n"); } */

/* static int64_t handle_info(options *) { printf("Handling command info\n"); } */

/* static int64_t handle_init(options *) { printf("Handling command init\n"); } */

/* static int64_t handle_install(options *) { printf("Handling command install\n"); } */

/* static int64_t handle_install_update(options *) { */
/*         printf("Handling command install-update\n"); */
/* } */

/* static int64_t handle_install_update_config(options *) { */
/*         printf("Handling command install-update-config\n"); */
/* } */

/* static int64_t handle_locate_project(options *) { */
/*         printf("Handling command locate-project\n"); */
/* } */

/* static int64_t handle_login(options *) { printf("Handling command login\n"); } */

/* static int64_t handle_logout(options *) { printf("Handling command logout\n"); } */

/* static int64_t handle_machete(options *) { printf("Handling command machete\n"); } */

/* static int64_t handle_metadata(options *) { printf("Handling command metadata\n"); } */

/* static int64_t handle_miri(options *) { printf("Handling command miri\n"); } */

/* static int64_t handle_new(options *) { printf("Handling command new\n"); } */

/* static int64_t handle_owner(options *) { printf("Handling command owner\n"); } */

/* static int64_t handle_package(options *) { printf("Handling command package\n"); } */

/* static int64_t handle_pkgid(options *) { printf("Handling command pkgid\n"); } */

/* static int64_t handle_publish(options *) { printf("Handling command publish\n"); } */

/* static int64_t handle_remove(options *) { printf("Handling command remove\n"); } */

/* static int64_t handle_report(options *) { printf("Handling command report\n"); } */

/* static int64_t handle_rm(options *) { printf("Handling command rm\n"); } */

/* static int64_t handle_run(options *) { printf("Handling command run\n"); } */

/* static int64_t handle_rustc(options *) { printf("Handling command rustc\n"); } */

/* static int64_t handle_search(options *) { printf("Handling command search\n"); } */

/* static int64_t handle_test(options *) { printf("Handling command test\n"); } */

/* static int64_t handle_tree(options *) { printf("Handling command tree\n"); } */

/* static int64_t handle_uninstall(options *) { printf("Handling command uninstall\n"); } */

/* static int64_t handle_update(options *) { printf("Handling command update\n"); } */

/* static int64_t handle_vendor(options *) { printf("Handling command vendor\n"); } */

/* static int64_t handle_version(options *) { printf("Handling command version\n"); } */

/* static int64_t handle_yank(options *) { printf("Handling command yank\n"); } */

command_s commands[] = {
    {
        .name = "add",
        .description = "Add dependencies to a manifest file",
        .action = &handle_add
    },
    {
        .name = "build",
        .description = "Compile a local package and all of its dependencies",
        .action = &handle_build
    },
    {
        .name = "b",
        .description = "alias: build",
        .action = &handle_build
    },
    /* { */
    /*     .name = "c", */
    /*     .description = "alias: check", */
    /*     .action = &handle_check */
    /* }, */
    /* { */
    /*     .name = "check", */
    /*     .description = "Check a local package and all of its dependencies", */
    /*     .action = &handle_check */
    /* }, */
    /* { */
    /*     .name = "clean", */
    /*     .description = "Remove generated artifacts", */
    /*     .action = &handle_clean */
    /* }, */
    /* { */
    /*     .name = "config", */
    /*     .description = "Inspect configuration values", */
    /*     .action = &handle_config */
    /* }, */
    /* { */
    /*     .name = "d", */
    /*     .description = "alias: doc", */
    /*     .action = &handle_doc */
    /* }, */
    /* { */
    /*     .name = "doc", */
    /*     .description = "Build a package's documentation", */
    /*     .action = &handle_doc */
    /* }, */
    /* { */
    /*     .name = "fetch", */
    /*     .description = "Fetch dependencies of a package from the network", */
    /*     .action = &handle_fetch */
    /* }, */
    /* { */
    /*     .name = "fix", */
    /*     .description = "Automatically fix lint warnings reported by rustc", */
    /*     .action = &handle_fix */
    /* }, */
    /* { */
    /*     .name = "fmt", */
    /*     .description = "Formats all bin and lib files of the current crate using rustfmt.", */
    /*     .action = &handle_fmt */
    /* }, */
    /* { */
    /*     .name = "generate-lockfile", */
    /*     .description = "Generate the lockfile for a package", */
    /*     .action = &handle_generate */
    /* }, */
    /* { */
    /*     .name = "help", */
    /*     .description = "Displays help for a cargo subcommand", */
    /*     .action = &handle_help */
    /* }, */
    /* { */
    /*     .name = "info", */
    /*     .description = "Display information about a package", */
    /*     .action = &handle_info */
    /* }, */
    /* { */
    /*     .name = "init", */
    /*     .description = "Create a new cargo package in an existing", */
    /*     .action = &handle_init */
    /* }, */
    /* { */
    /*     .name = "lint", */
    /*     .description = "Lint the package", */
    /*     .action = &handle_lint */
    /* }, */
    /* { */
    /*     .name = "install", */
    /*     .description = "Install a Rust binary", */
    /*     .action = &handle_install */
    /* }, */
    /* { */
    /*     .name = "install-update", */
    /*     .description = "update", */
    /*     .action = &handle_install */
    /* }, */
    /* { */
    /*     .name = "install-update-config", */
    /*     .description = "update-config", */
    /*     .action = &handle_install */
    /* }, */
    /* { */
    /*     .name = "locate-project", */
    /*     .description = "Print a JSON representation of a Cargo.toml", */
    /*     .action = &handle_locate */
    /* }, */
    /* { */
    /*     .name = "login", */
    /*     .description = "Log in to a registry.", */
    /*     .action = &handle_login */
    /* }, */
    /* { */
    /*     .name = "logout", */
    /*     .description = "Remove an API token from the registry locally", */
    /*     .action = &handle_logout */
    /* }, */
    /* { */
    /*     .name = "machete", */
    /*     .description = ""}, */
    /* .action = &handle_machete */
    /* { */
    /*     .name = "metadata", */
    /*     .description = "Output the resolved dependencies of a", */
    /*     .action = &handle_metadata */
    /* }, */
    /* { */
    /*     .name = "package", */
    /*     .description = "the concrete used versions including overrides, in machine-readable format", */
    /*     .action = &handle_package */
    /* }, */
    /* { */
    /*     .name = "miri", */
    /*     .description = "" */
    /*     .action = &handle_miri */
    /* }, */
    /* { */
    /*     .name = "new", */
    /*     .description = "Create a new package at <path>", */
    /*     .action = &handle_new */
    /* }, */
    /* { */
    /*     .name = "owner", */
    /*     .description = "Manage the owners of a crate on the registry", */
    /*     .action = &handle_owner */
    /* }, */
    /* { */
    /*     .name = "package", */
    /*     .description = "Assemble the local package into a", */
    /*     .action = &handle_package */
    /* }, */
    /* { */
    /*     .name = "distributable", */
    /*     .description = "tarball", */
    /*     .action = &handle_distributable */
    /* }, */
    /* { */
    /*     .name = "pkgid", */
    /*     .description = "Print a fully qualified package specification", */
    /*     .action = &handle_pkgid */
    /* }, */
    /* { */
    /*     .name = "publish", */
    /*     .description = "Upload a package to the registry", */
    /*     .action = &handle_publish */
    /* }, */
    /* { */
    /*     .name = "r", */
    /*     .description = "alias: run", */
    /*     .action = &handle_run */
    /* }, */
    /* { */
    /*     .name = "remove", */
    /*     .description = "Remove dependencies from a manifest file", */
    /*     .action = &handle_remove */
    /* }, */
    /* { */
    /*     .name = "report", */
    /*     .description = "Generate and display various kinds of reports", */
    /*     .action = &handle_report */
    /* }, */
    /* { */
    /*     .name = "rm", */
    /*     .description = "alias: remove", */
    /*     .action = &handle_rm */
    /* }, */
    /* { */
    /*     .name = "run", */
    /*     .description = "Run a binary or example of the local package", */
    /*     .action = &handle_run */
    /* }, */
    /* { */
    /*     .name = "compile", */
    /*     .description = "Compile a package, and pass extra options to", */
    /*     .action = &handle_compile */
    /* }, */
    /* { */
    /*     .name = "search", */
    /*     .description = "Search packages in the registry. Default", */
    /*     .action = &handle_search */
    /* }, */
    /* { */
    /*     .name = "t", */
    /*     .description = "alias: test", */
    /*     .action = &handle_test */
    /* }, */
    /* { */
    /*     .name = "test", */
    /*     .description = "Execute all unit and integration tests and", */
    /*     .action = &handle_test */
    /* }, */
    /* { */
    /*     .name = "tree", */
    /*     .description = "Display a tree visualization of a dependency", */
    /*     .action = &handle_tree */
    /* }, */
    /* { */
    /*     .name = "uninstall", */
    /*     .description = "Remove a Rust binary", */
    /*     .action = &handle_uninstall */
    /* }, */
    /* { */
    /*     .name = "update", */
    /*     .description = "Update dependencies as recorded in the local", */
    /*     .action = &handle_update */
    /* }, */
    /* { */
    /*     .name = "vendor", */
    /*     .description = "Vendor all dependencies for a project locally", */
    /*     .action = &handle_vendor */
    /* }, */
    /* { */
    /*     .name = "version", */
    /*     .description = "Show version information", */
    /*     .action = &handle_version */
    /* }, */
    /* { */
    /*     .name = "yank", */
    /*     .description = "Remove a pushed crate from the index", */
    /*     .action = &handle_yank */
    /* }, */
};

int main(int argc, char **argv) {
    static struct gengetopt_args_info args_info;
    cmdline_parser(argc, argv, &args_info);

    printf("Arguments number: %d\n", args_info.inputs_num);
    int64_t toolchain_idx = -1;
    for (int64_t i = 0; i < args_info.inputs_num; i++) {
        if (args_info.inputs[i][0] == '+') {
            toolchain_idx = i;
            printf("Toolchain: %s (%w64d)\n", args_info.inputs[i],
                   toolchain_idx);
            break;
        }
    }

    /*
     * the command is the first unnamed option that is not the
     * toolchain. Therefore it is at index 0 if the toolchain is in
     * the second place (or later), while it is at index 1 if the
     * first place is taken by the toolchain. The value is -1 if
     * there is no command.
     * */
    int command_idx = -1;
    printf("Arguments number: %d\n", args_info.inputs_num);
    if (args_info.inputs_num > 0) {
        if ((toolchain_idx < 0) && (args_info.inputs_num >= 1)) {
            command_idx = 0;
        }
        if (toolchain_idx > 0) {
            command_idx = 0;
        }
        if ((toolchain_idx == 0) && (args_info.inputs_num >= 2)) {
            command_idx = 1;
        }
    }

    if (command_idx < 0) {
        /* no command given */
        if (args_info.list_given) {
            printf("Installed Commands:\n");
            for (int64_t i = 0; i < lengthof(commands); i++) {
                printf("%s\t%s\n", commands[i].name, commands[i].description);
            }
            printf("No command given\n");
            exit(EXIT_SUCCESS);
        }

        char *cmd = args_info.inputs[command_idx];
        printf("Command is: %s (%d).\n", cmd, command_idx);

        if (strcmp(cmd, "add") == 0) {
            handle_add(0);
            exit(EXIT_SUCCESS);
        }
        printf("Command '%s' is not recognized.\n", cmd);
        return 0;
    }
}
