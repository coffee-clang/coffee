/*
  Copyright (C) 2025 by the coffee developers

*/

#include "coffee.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
        CMD_UNKNOWN = -1,
        CMD_ADD,
        CMD_B,
        CMD_BENCH,
        CMD_BUILD,
        CMD_C,
        CMD_CHECK,
        CMD_CLEAN,
        CMD_CLIPPY,
        CMD_CONFIG,
        CMD_D,
        CMD_DOC,
        CMD_FETCH,
        CMD_FIX,
        CMD_FMT,
        CMD_GENERATE_LOCKFILE,
        CMD_HELP,
        CMD_INFO,
        CMD_INIT,
        CMD_INSTALL,
        CMD_INSTALL_UPDATE,
        CMD_INSTALL_UPDATE_CONFIG,
        CMD_LOCATE_PROJECT,
        CMD_LOGIN,
        CMD_LOGOUT,
        CMD_MACHETE,
        CMD_METADATA,
        CMD_MIRI,
        CMD_NEW,
        CMD_OWNER,
        CMD_PACKAGE,
        CMD_PKGID,
        CMD_PUBLISH,
        CMD_R,
        CMD_REMOVE,
        CMD_REPORT,
        CMD_RM,
        CMD_RUN,
        CMD_RUSTC,
        CMD_RUSTDOC,
        CMD_SEARCH,
        CMD_T,
        CMD_TEST,
        CMD_TREE,
        CMD_UNINSTALL,
        CMD_UPDATE,
        CMD_VENDOR,
        CMD_VERSION,
        CMD_YANK
} command_t;

static command_t parse_command(const char *cmd) {
    if (strcmp(cmd, "add") == 0)
            return CMD_ADD;
    else if (strcmp(cmd, "b") == 0)
            return CMD_B;
    else if (strcmp(cmd, "bench") == 0)
            return CMD_BENCH;
    else if (strcmp(cmd, "build") == 0)
            return CMD_BUILD;
    else if (strcmp(cmd, "c") == 0)
            return CMD_C;
    else if (strcmp(cmd, "check") == 0)
            return CMD_CHECK;
    else if (strcmp(cmd, "clean") == 0)
            return CMD_CLEAN;
    else if (strcmp(cmd, "clippy") == 0)
            return CMD_CLIPPY;
    else if (strcmp(cmd, "config") == 0)
            return CMD_CONFIG;
    else if (strcmp(cmd, "d") == 0)
            return CMD_D;
    else if (strcmp(cmd, "doc") == 0)
            return CMD_DOC;
    else if (strcmp(cmd, "fetch") == 0)
            return CMD_FETCH;
    else if (strcmp(cmd, "fix") == 0)
            return CMD_FIX;
    else if (strcmp(cmd, "fmt") == 0)
            return CMD_FMT;
    else if (strcmp(cmd, "generate-lockfile") == 0)
            return CMD_GENERATE_LOCKFILE;
    else if (strcmp(cmd, "help") == 0)
            return CMD_HELP;
    else if (strcmp(cmd, "info") == 0)
            return CMD_INFO;
    else if (strcmp(cmd, "init") == 0)
            return CMD_INIT;
    else if (strcmp(cmd, "install") == 0)
            return CMD_INSTALL;
    else if (strcmp(cmd, "install-update") == 0)
            return CMD_INSTALL_UPDATE;
    else if (strcmp(cmd, "install-update-config") == 0)
            return CMD_INSTALL_UPDATE_CONFIG;
    else if (strcmp(cmd, "locate-project") == 0)
            return CMD_LOCATE_PROJECT;
    else if (strcmp(cmd, "login") == 0)
            return CMD_LOGIN;
    else if (strcmp(cmd, "logout") == 0)
            return CMD_LOGOUT;
    else if (strcmp(cmd, "machete") == 0)
            return CMD_MACHETE;
    else if (strcmp(cmd, "metadata") == 0)
            return CMD_METADATA;
    else if (strcmp(cmd, "miri") == 0)
            return CMD_MIRI;
    else if (strcmp(cmd, "new") == 0)
            return CMD_NEW;
    else if (strcmp(cmd, "owner") == 0)
            return CMD_OWNER;
    else if (strcmp(cmd, "package") == 0)
            return CMD_PACKAGE;
    else if (strcmp(cmd, "pkgid") == 0)
            return CMD_PKGID;
    else if (strcmp(cmd, "publish") == 0)
            return CMD_PUBLISH;
    else if (strcmp(cmd, "r") == 0)
            return CMD_R;
    else if (strcmp(cmd, "remove") == 0)
            return CMD_REMOVE;
    else if (strcmp(cmd, "report") == 0)
            return CMD_REPORT;
    else if (strcmp(cmd, "rm") == 0)
            return CMD_RM;
    else if (strcmp(cmd, "run") == 0)
            return CMD_RUN;
    else if (strcmp(cmd, "rustc") == 0)
            return CMD_RUSTC;
    else if (strcmp(cmd, "rustdoc") == 0)
            return CMD_RUSTDOC;
    else if (strcmp(cmd, "search") == 0)
            return CMD_SEARCH;
    else if (strcmp(cmd, "t") == 0)
            return CMD_T;
    else if (strcmp(cmd, "test") == 0)
            return CMD_TEST;
    else if (strcmp(cmd, "tree") == 0)
            return CMD_TREE;
    else if (strcmp(cmd, "uninstall") == 0)
            return CMD_UNINSTALL;
    else if (strcmp(cmd, "update") == 0)
            return CMD_UPDATE;
    else if (strcmp(cmd, "vendor") == 0)
            return CMD_VENDOR;
    else if (strcmp(cmd, "version") == 0)
            return CMD_VERSION;
    else if (strcmp(cmd, "yank") == 0)
            return CMD_YANK;
    else
            return CMD_UNKNOWN;
}

static void handle_add(void) {
    printf("Handling command add\n");
}

static void handle_b(void) {
    printf("Handling command b\n");
}

static void handle_bench(void) {
    printf("Handling command bench\n");
}

static void handle_build(void) {
    printf("Handling command build\n");
}

static void handle_c(void) {
    printf("Handling command c\n");
}

static void handle_check(void) {
    printf("Handling command check\n");
}

static void handle_clean(void) {
    printf("Handling command clean\n");
}

static void handle_clippy(void) {
    printf("Handling command clippy\n");
}

static void handle_config(void) {
    printf("Handling command config\n");
}

static void handle_d(void) {
    printf("Handling command d\n");
}

static void handle_doc(void) {
    printf("Handling command doc\n");
}

static void handle_fetch(void) {
    printf("Handling command fetch\n");
}

static void handle_fix(void) {
    printf("Handling command fix\n");
}

static void handle_fmt(void) {
    printf("Handling command fmt\n");
}

static void handle_generate_lockfile(void) {
    printf("Handling command generate-lockfile\n");
}

static void handle_help(void) {
    printf("Handling command help\n");
}

static void handle_info(void) {
    printf("Handling command info\n");
}

static void handle_init(void) {
    printf("Handling command init\n");
}

static void handle_install(void) {
    printf("Handling command install\n");
}

static void handle_install_update(void) {
    printf("Handling command install-update\n");
}

static void handle_install_update_config(void) {
    printf("Handling command install-update-config\n");
}

static void handle_locate_project(void) {
    printf("Handling command locate-project\n");
}

static void handle_login(void) {
    printf("Handling command login\n");
}

static void handle_logout(void) {
    printf("Handling command logout\n");
}

static void handle_machete(void) {
    printf("Handling command machete\n");
}

static void handle_metadata(void) {
    printf("Handling command metadata\n");
}

static void handle_miri(void) {
    printf("Handling command miri\n");
}

static void handle_new(void) {
    printf("Handling command new\n");
}

static void handle_owner(void) {
    printf("Handling command owner\n");
}

static void handle_package(void) {
    printf("Handling command package\n");
}

static void handle_pkgid(void) {
    printf("Handling command pkgid\n");
}

static void handle_publish(void) {
    printf("Handling command publish\n");
}

static void handle_r(void) {
    printf("Handling command r\n");
}

static void handle_remove(void) {
    printf("Handling command remove\n");
}

static void handle_report(void) {
    printf("Handling command report\n");
}

static void handle_rm(void) {
    printf("Handling command rm\n");
}

static void handle_run(void) {
    printf("Handling command run\n");
}

static void handle_rustc(void) {
    printf("Handling command rustc\n");
}

static void handle_rustdoc(void) {
    printf("Handling command rustdoc\n");
}

static void handle_search(void) {
    printf("Handling command search\n");
}

static void handle_t(void) {
    printf("Handling command t\n");
}

static void handle_test(void) {
    printf("Handling command test\n");
}

static void handle_tree(void) {
    printf("Handling command tree\n");
}

static void handle_uninstall(void) {
    printf("Handling command uninstall\n");
}

static void handle_update(void) {
    printf("Handling command update\n");
}

static void handle_vendor(void) {
    printf("Handling command vendor\n");
}

static void handle_version(void) {
    printf("Handling command version\n");
}

static void handle_yank(void) {
    printf("Handling command yank\n");
}

int main(int argc, char **argv) {
        static struct gengetopt_args_info args_info;
        cmdline_parser(argc, argv, &args_info);

        int64_t toolchain_idx = -1;
        for (int64_t i = 0; i < args_info.inputs_num; i++) {
                if (args_info.inputs[i][0] == '+') {
                        toolchain_idx = i;
                        printf("Command: %s\n", args_info.inputs[i]);
                        break;
                }
        }

        /* 
         * the command is the first unnamed option that is not the toolchain.
         * Therefore it is at index 0 if the toolchain is in the second place
         * (or later), while it is at index 1 if the first place is taken by
         * the toolchain.
         * The value is -1 if there is no command 
         */
        int64_t command_idx = -1;
        if (args_info.inputs_num > 0) {
                if (toolchain_idx == 0 && args_info.inputs_num > 1) {
                        command_idx = 1;
                } else if (toolchain_idx != 0) {
                        command_idx = 0;
                }
        }

        if (command_idx != -1) {
                command_t command = parse_command(args_info.inputs[command_idx]);
                switch (command) {
                        case CMD_ADD:
                                handle_add();
                                break;
                        case CMD_B:
                                handle_b();
                                break;
                        case CMD_BENCH:
                                handle_bench();
                                break;
                        case CMD_BUILD:
                                handle_build();
                                break;
                        case CMD_C:
                                handle_c();
                                break;
                        case CMD_CHECK:
                                handle_check();
                                break;
                        case CMD_CLEAN:
                                handle_clean();
                                break;
                        case CMD_CLIPPY:
                                handle_clippy();
                                break;
                        case CMD_CONFIG:
                                handle_config();
                                break;
                        case CMD_D:
                                handle_d();
                                break;
                        case CMD_DOC:
                                handle_doc();
                                break;
                        case CMD_FETCH:
                                handle_fetch();
                                break;
                        case CMD_FIX:
                                handle_fix();
                                break;
                        case CMD_FMT:
                                handle_fmt();
                                break;
                        case CMD_GENERATE_LOCKFILE:
                                handle_generate_lockfile();
                                break;
                        case CMD_HELP:
                                handle_help();
                                break;
                        case CMD_INFO:
                                handle_info();
                                break;
                        case CMD_INIT:
                                handle_init();
                                break;
                        case CMD_INSTALL:
                                handle_install();
                                break;
                        case CMD_INSTALL_UPDATE:
                                handle_install_update();
                                break;
                        case CMD_INSTALL_UPDATE_CONFIG:
                                handle_install_update_config();
                                break;
                        case CMD_LOCATE_PROJECT:
                                handle_locate_project();
                                break;
                        case CMD_LOGIN:
                                handle_login();
                                break;
                        case CMD_LOGOUT:
                                handle_logout();
                                break;
                        case CMD_MACHETE:
                                handle_machete();
                                break;
                        case CMD_METADATA:
                                handle_metadata();
                                break;
                        case CMD_MIRI:
                                handle_miri();
                                break;
                        case CMD_NEW:
                                handle_new();
                                break;
                        case CMD_OWNER:
                                handle_owner();
                                break;
                        case CMD_PACKAGE:
                                handle_package();
                                break;
                        case CMD_PKGID:
                                handle_pkgid();
                                break;
                        case CMD_PUBLISH:
                                handle_publish();
                                break;
                        case CMD_R:
                                handle_r();
                                break;
                        case CMD_REMOVE:
                                handle_remove();
                                break;
                        case CMD_REPORT:
                                handle_report();
                                break;
                        case CMD_RM:
                                handle_rm();
                                break;
                        case CMD_RUN:
                                handle_run();
                                break;
                        case CMD_RUSTC:
                                handle_rustc();
                                break;
                        case CMD_RUSTDOC:
                                handle_rustdoc();
                                break;
                        case CMD_SEARCH:
                                handle_search();
                                break;
                        case CMD_T:
                                handle_t();
                                break;
                        case CMD_TEST:
                                handle_test();
                                break;
                        case CMD_TREE:
                                handle_tree();
                                break;
                        case CMD_UNINSTALL:
                                handle_uninstall();
                                break;
                        case CMD_UPDATE:
                                handle_update();
                                break;
                        case CMD_VENDOR:
                                handle_vendor();
                                break;
                        case CMD_VERSION:
                                handle_version();
                                break;
                        case CMD_YANK:
                                handle_yank();
                                break;
                        default:
                                printf("Command '%s' is not recognized.\n", args_info.inputs[command_idx]);
                                break;
                }
        }
        return 0;
}
