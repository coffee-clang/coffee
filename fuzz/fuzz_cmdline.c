/**
 * Fuzz harness for cmdline_parser().
 * Constructs argv from fuzz input, splitting on null bytes.
 */

#include "cmdline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
	if (Size < 2 || Size > 4096) {
		return 0;
	}

	/* Count null-byte-separated arguments */
	size_t argc = 1; /* argv[0] = program name */
	for (size_t i = 0; i < Size; i++) {
		if (Data[i] == '\0') {
			argc++;
		}
	}

	/* Build argv */
	char **argv = calloc(argc + 1, sizeof(char *));
	if (argv == nullptr) {
		return 0;
	}

	argv[0] = strdup("coffee");
	if (argv[0] == nullptr) {
		free(argv);
		return 0;
	}

	size_t arg_idx = 1;
	size_t start   = 0;
	for (size_t i = 0; i <= Size && arg_idx < argc; i++) {
		if (i == Size || Data[i] == '\0') {
			size_t len = i - start;
			if (len > 0) {
				argv[arg_idx] = malloc(len + 1);
				if (argv[arg_idx]) {
					memcpy(argv[arg_idx], Data + start, len);
					argv[arg_idx][len] = '\0';
					arg_idx++;
				}
			}
			start = i + 1;
		}
	}

	/* Set the rest to nullptr */
	for (size_t i = arg_idx; i <= argc; i++) {
		argv[i] = nullptr;
	}

	/* Run the parser */
	struct cli_args args;
	cmdline_parser_init(&args);
	cmdline_parser((i64)argc, argv, &args);
	cmdline_parser_free(&args);

	/* Cleanup */
	for (size_t i = 0; i <= argc && argv[i]; i++) {
		free(argv[i]);
	}
	free(argv);

	return 0;
}
