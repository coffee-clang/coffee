/*
  Copyright (C) 2025 by the coffee developers

*/

#include "coffee.h"
#include <stdio.h>
#include <stdint.h>
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
        int64_t non_toolchain_idx = -1;
        for (int64_t i = 0; i < args_info.inputs_num; i++) {
                if (i == toolchain_idx) {
                        continue;
                }
                non_toolchain_idx = i;
                printf("Non-toolchain command: %s\n", args_info.inputs[i]);
                break;
        }
}
