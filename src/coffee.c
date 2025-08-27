/*
  Copyright (C) 2025 by the coffee developers

*/

#include "coffee.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
                char *cmd = args_info.inputs[command_idx];
                if (strcmp(cmd, "add") == 0) {
                        handle_add();
                } else if (strcmp(cmd, "b") == 0) {
                        handle_b();
                } else if (strcmp(cmd, "bench") == 0) {
                        handle_bench();
                } else if (strcmp(cmd, "build") == 0) {
                        handle_build();
                } else if (strcmp(cmd, "c") == 0) {
                        handle_c();
                } else if (strcmp(cmd, "check") == 0) {
                        handle_check();
                } else if (strcmp(cmd, "clean") == 0) {
                        handle_clean();
                } else if (strcmp(cmd, "clippy") == 0) {
                        handle_clippy();
                } else if (strcmp(cmd, "config") == 0) {
                        handle_config();
                } else if (strcmp(cmd, "d") == 0) {
                        handle_d();
                } else if (strcmp(cmd, "doc") == 0) {
                        handle_doc();
                } else if (strcmp(cmd, "fetch") == 0) {
                        handle_fetch();
                } else if (strcmp(cmd, "fix") == 0) {
                        handle_fix();
                } else if (strcmp(cmd, "fmt") == 0) {
                        handle_fmt();
                } else if (strcmp(cmd, "generate-lockfile") == 0) {
                        handle_generate_lockfile();
                } else if (strcmp(cmd, "help") == 0) {
                        handle_help();
                } else if (strcmp(cmd, "info") == 0) {
                        handle_info();
                } else if (strcmp(cmd, "init") == 0) {
                        handle_init();
                } else if (strcmp(cmd, "install") == 0) {
                        handle_install();
                } else if (strcmp(cmd, "install-update") == 0) {
                        handle_install_update();
                } else if (strcmp(cmd, "install-update-config") == 0) {
                        handle_install_update_config();
                } else if (strcmp(cmd, "locate-project") == 0) {
                        handle_locate_project();
                } else if (strcmp(cmd, "login") == 0) {
                        handle_login();
                } else if (strcmp(cmd, "logout") == 0) {
                        handle_logout();
                } else if (strcmp(cmd, "machete") == 0) {
                        handle_machete();
                } else if (strcmp(cmd, "metadata") == 0) {
                        handle_metadata();
                } else if (strcmp(cmd, "miri") == 0) {
                        handle_miri();
                } else if (strcmp(cmd, "new") == 0) {
                        handle_new();
                } else if (strcmp(cmd, "owner") == 0) {
                        handle_owner();
                } else if (strcmp(cmd, "package") == 0) {
                        handle_package();
                } else if (strcmp(cmd, "pkgid") == 0) {
                        handle_pkgid();
                } else if (strcmp(cmd, "publish") == 0) {
                        handle_publish();
                } else if (strcmp(cmd, "r") == 0) {
                        handle_r();
                } else if (strcmp(cmd, "remove") == 0) {
                        handle_remove();
                } else if (strcmp(cmd, "report") == 0) {
                        handle_report();
                } else if (strcmp(cmd, "rm") == 0) {
                        handle_rm();
                } else if (strcmp(cmd, "run") == 0) {
                        handle_run();
                } else if (strcmp(cmd, "rustc") == 0) {
                        handle_rustc();
                } else if (strcmp(cmd, "rustdoc") == 0) {
                        handle_rustdoc();
                } else if (strcmp(cmd, "search") == 0) {
                        handle_search();
                } else if (strcmp(cmd, "t") == 0) {
                        handle_t();
                } else if (strcmp(cmd, "test") == 0) {
                        handle_test();
                } else if (strcmp(cmd, "tree") == 0) {
                        handle_tree();
                } else if (strcmp(cmd, "uninstall") == 0) {
                        handle_uninstall();
                } else if (strcmp(cmd, "update") == 0) {
                        handle_update();
                } else if (strcmp(cmd, "vendor") == 0) {
                        handle_vendor();
                } else if (strcmp(cmd, "version") == 0) {
                        handle_version();
                } else if (strcmp(cmd, "yank") == 0) {
                        handle_yank();
                } else {
                        printf("Command '%s' is not recognized.\n", cmd);
                }
        }
        return 0;
}
