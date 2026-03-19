#include "../coffee.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t handle_check(options *)
{
	printf("Checking source code for syntax errors...\n");

	const char *cc = getenv("CC") ? getenv("CC") : "clang";

	// Use find to get all .c files in src and tests
	// and run clang/gcc -fsyntax-only on them.
	int ret = system("find src tests -name \"*.c\" | xargs clang -fsyntax-only -Isrc 2>/dev/null");

	if (ret != 0) {
		fprintf(stderr, "Error: Check failed. Syntax errors found.\n");
		return 1;
	}

	printf("Check complete. No syntax errors found.\n");
	return 0;
}
