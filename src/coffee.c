/*
  Copyright (C) 2025 by the coffee developers

*/

#include "coffee.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void handle_add(void) { printf("Handling command add\n"); }

static void handle_bench(void) { printf("Handling command bench\n"); }

static void handle_build(void) { printf("Handling command build\n"); }

static void handle_check(void) { printf("Handling command check\n"); }

static void handle_clean(void) { printf("Handling command clean\n"); }

static void handle_clippy(void) { printf("Handling command clippy\n"); }

static void handle_config(void) { printf("Handling command config\n"); }

static void handle_doc(void) { printf("Handling command doc\n"); }

static void handle_fetch(void) { printf("Handling command fetch\n"); }

static void handle_fix(void) { printf("Handling command fix\n"); }

static void handle_fmt(void) { printf("Handling command fmt\n"); }

static void handle_generate_lockfile(void) {
    printf("Handling command generate-lockfile\n");
}

static void handle_help(void) { printf("Handling command help\n"); }

static void handle_info(void) { printf("Handling command info\n"); }

static void handle_init(void) { printf("Handling command init\n"); }

static void handle_install(void) { printf("Handling command install\n"); }

static void handle_install_update(void) {
    printf("Handling command install-update\n");
}

static void handle_install_update_config(void) {
    printf("Handling command install-update-config\n");
}

static void handle_locate_project(void) {
    printf("Handling command locate-project\n");
}

static void handle_login(void) { printf("Handling command login\n"); }

static void handle_logout(void) { printf("Handling command logout\n"); }

static void handle_machete(void) { printf("Handling command machete\n"); }

static void handle_metadata(void) { printf("Handling command metadata\n"); }

static void handle_miri(void) { printf("Handling command miri\n"); }

static void handle_new(void) { printf("Handling command new\n"); }

static void handle_owner(void) { printf("Handling command owner\n"); }

static void handle_package(void) { printf("Handling command package\n"); }

static void handle_pkgid(void) { printf("Handling command pkgid\n"); }

static void handle_publish(void) { printf("Handling command publish\n"); }

static void handle_remove(void) { printf("Handling command remove\n"); }

static void handle_report(void) { printf("Handling command report\n"); }

static void handle_rm(void) { printf("Handling command rm\n"); }

static void handle_run(void) { printf("Handling command run\n"); }

static void handle_rustc(void) { printf("Handling command rustc\n"); }

static void handle_search(void) { printf("Handling command search\n"); }

static void handle_test(void) { printf("Handling command test\n"); }

static void handle_tree(void) { printf("Handling command tree\n"); }

static void handle_uninstall(void) { printf("Handling command uninstall\n"); }

static void handle_update(void) { printf("Handling command update\n"); }

static void handle_vendor(void) { printf("Handling command vendor\n"); }

static void handle_version(void) { printf("Handling command version\n"); }

static void handle_yank(void) { printf("Handling command yank\n"); }

int main(int argc, char **argv) {
    static struct gengetopt_args_info args_info;
    cmdline_parser(argc, argv, &args_info);

    printf("Arguments number: %d\n", args_info.inputs_num);
    int64_t toolchain_idx = -1;
    for (int64_t i = 0; i < args_info.inputs_num; i++) {
        if (args_info.inputs[i][0] == '+') {
            toolchain_idx = i;
            printf("Toolchain: %s (%d)\n", args_info.inputs[i], toolchain_idx);
            break;
        }
    }

    /*
     * the command is the first unnamed option that is not the toolchain.
     * Therefore it is at index 0 if the toolchain is in the second place
     * (or later), while it is at index 1 if the first place is taken by
     * the toolchain.
     * The value is -1 if there is no command.
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
        printf("No command given\n");
        exit(EXIT_SUCCESS);
    }

    char *cmd = args_info.inputs[command_idx];
    printf("Command is: %s (%d).\n", cmd, command_idx);

    if (strcmp(cmd, "add") == 0) {
        handle_add();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "bench") == 0) {
        handle_bench();
        exit(EXIT_SUCCESS);
    }
    if ((strcmp(cmd, "build") == 0) || (strcmp(cmd, "b") == 0)) {
        handle_build();
        exit(EXIT_SUCCESS);
    }
    if ((strcmp(cmd, "check") == 0) || (strcmp(cmd, "c") == 0)) {
        handle_check();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "clean") == 0) {
        handle_clean();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "config") == 0) {
        handle_config();
        exit(EXIT_SUCCESS);
    }
    if ((strcmp(cmd, "doc") == 0) || (strcmp(cmd, "d") == 0)) {
        handle_doc();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "fetch") == 0) {
        handle_fetch();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "fix") == 0) {
        handle_fix();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "fmt") == 0) {
        handle_fmt();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "generate-lockfile") == 0) {
        handle_generate_lockfile();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "help") == 0) {
        handle_help();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "info") == 0) {
        handle_info();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "init") == 0) {
        handle_init();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "install") == 0) {
        handle_install();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "install-update") == 0) {
        handle_install_update();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "install-update-config") == 0) {
        handle_install_update_config();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "lint") == 0) {
        handle_clippy();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "locate-project") == 0) {
        handle_locate_project();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "login") == 0) {
        handle_login();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "logout") == 0) {
        handle_logout();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "machete") == 0) {
        handle_machete();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "metadata") == 0) {
        handle_metadata();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "miri") == 0) {
        handle_miri();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "new") == 0) {
        handle_new();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "owner") == 0) {
        handle_owner();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "package") == 0) {
        handle_package();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "pkgid") == 0) {
        handle_pkgid();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "publish") == 0) {
        handle_publish();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "remove") == 0) {
        handle_remove();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "report") == 0) {
        handle_report();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "rm") == 0) {
        handle_rm();
        exit(EXIT_SUCCESS);
    }
    if ((strcmp(cmd, "run") == 0) || (strcmp(cmd, "r") == 0)) {
        handle_run();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "rustc") == 0) {
        handle_rustc();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "search") == 0) {
        handle_search();
        exit(EXIT_SUCCESS);
    }
    if ((strcmp(cmd, "test") == 0) || (strcmp(cmd, "t") == 0)) {
        handle_test();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "tree") == 0) {
        handle_tree();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "uninstall") == 0) {
        handle_uninstall();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "update") == 0) {
        handle_update();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "vendor") == 0) {
        handle_vendor();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "version") == 0) {
        handle_version();
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd, "yank") == 0) {
        handle_yank();
        exit(EXIT_SUCCESS);
    } else {
        printf("Command '%s' is not recognized.\n", cmd);
    }
    return 0;
}
