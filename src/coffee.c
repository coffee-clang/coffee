/*
  Copyright (C) 2025 by the coffee developers

*/

#include "coffee.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
                } else {
                        command_idx = 0;
                }
        }

        /*
         add
         b
         bench
         build
         c
         check
         clean
         clippy
         config
         d
         doc
         fetch
         fix
         fmt
         generate-lockfile
         help
         info
         init
         install
         install-update
         install-update-config
         locate-project
         login
         logout
         machete
         metadata
         miri
         new
         owner
         package
         pkgid
         publish
         r
         remove
         report
         rm
         run
         rustc
         rustdoc
         search
         t
         test
         tree
         uninstall
         update
         vendor
         version
         yank
         */
        if (command_idx != -1) {
                const char *valid_commands[] = {
                        "add",
                        "b",
                        "bench",
                        "build",
                        "c",
                        "check",
                        "clean",
                        "clippy",
                        "config",
                        "d",
                        "doc",
                        "fetch",
                        "fix",
                        "fmt",
                        "generate-lockfile",
                        "help",
                        "info",
                        "init",
                        "install",
                        "install-update",
                        "install-update-config",
                        "locate-project",
                        "login",
                        "logout",
                        "machete",
                        "metadata",
                        "miri",
                        "new",
                        "owner",
                        "package",
                        "pkgid",
                        "publish",
                        "r",
                        "remove",
                        "report",
                        "rm",
                        "run",
                        "rustc",
                        "rustdoc",
                        "search",
                        "t",
                        "test",
                        "tree",
                        "uninstall",
                        "update",
                        "vendor",
                        "version",
                        "yank"
                };
                int valid = 0;
                size_t num_valid_commands = sizeof(valid_commands) / sizeof(valid_commands[0]);
                for (size_t i = 0; i < num_valid_commands; i++) {
                        if (strcmp(args_info.inputs[command_idx], valid_commands[i]) == 0) {
                                valid = 1;
                                break;
                        }
                }
                if (valid) {
                        printf("Command '%s' is recognized.\n", args_info.inputs[command_idx]);
                }
                else {
                        printf("Command '%s' is not recognized.\n", args_info.inputs[command_idx]);
                }
        }
}
