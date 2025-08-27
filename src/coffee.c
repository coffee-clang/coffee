/*
  Copyright (C) 2025 by the coffee developers

*/

#include "coffee.h"

int main(int argc, char **argv) {
        static struct gengetopt_args_info args_info;
        cmdline_parser(argc, argv, &args_info);

        // AI! go through the args_info.inputs list and find the first input
        // starting with a +
        for (int64_t i = 0; i < args_info.inputs_num; i++) {
                printf("Command: %s\n", args_info.inputs[i]);
        }
}
