#include "../coffee.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

int64_t handle_doc(options *opts)
{
	printf("Generating documentation...\n");

	if (access("Doxyfile", F_OK) == 0) {
		int ret = system("doxygen");
		if (ret != 0) {
			fprintf_safe(stderr, "Error: Documentation generation failed. Please ensure 'doxygen' is installed.\n");
			return 1;
		}
	} else {
		printf("No Doxyfile found. Please create one to use 'coffee doc'.\n");
		printf("Tip: Run 'doxygen -g' to generate a default Doxyfile.\n");
		return 0;
	}

	printf("Documentation complete.\n");
	return 0;
}
