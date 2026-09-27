#include "../cmdline.h"
#include "../coffee.h"

#include <stdio.h>

int64_t handle_help(options *opt)
{
	(void)opt;
	cmdline_parser_print_help();
	return 0;
}
