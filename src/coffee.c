/*
  Copyright (C) 2025 by the coffee developers

*/

#include "coffee.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
        /* the command is the first unnamed option that is not the toolchain.
         * Therefore it is at index 0 if the toolchain is in the second place
         * (or later), while it is at index 1 if the first place is taken by
         * the toolchain.
         * The value is -1 if there is no command */
        int64_t command_idx =
            (toolchain_idx > 0) ? 0 : ((args_info.inputs_num > 1) ? 0 : -1);

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
        // AI! test if args_info.inputs[command_idx] is one of the words in the
        // preceding comment
        for (int64_t i = 0; i < args_info.inputs_num; i++) {
                if (i == toolchain_idx) {
                        continue;
                }
                non_toolchain_idx = i;
                printf("Non-toolchain command: %s\n", args_info.inputs[i]);
                break;
        }
}
