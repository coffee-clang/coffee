SHELL := bash
.SHELLSFLAGS := -eu -o pipefail -c

.DEFAULT_GOAL := all

CC := clang
CSTD := c23

P := coffee
TARGET := bin/$(P)

SRC_DIR := src
DEPS_DIR := deps
BIN_DIR := bin

COMMANDS_SRC := $(wildcard $(SRC_DIR)/commands/*.c)
COMMANDS_OBJ := $(COMMANDS_SRC:$(SRC_DIR)/%.c=$(BIN_DIR)/%.o)

CORE_SRC := $(SRC_DIR)/coffee.c
CORE_OBJ := $(BIN_DIR)/coffee.o

MANIFEST_SRC := $(SRC_DIR)/manifest.c
MANIFEST_OBJ := $(BIN_DIR)/manifest.o

REGISTRY_SRC := $(SRC_DIR)/registry.c
REGISTRY_OBJ := $(BIN_DIR)/registry.o

PROJECT_SRC := $(SRC_DIR)/project.c
PROJECT_OBJ := $(BIN_DIR)/project.o

BUILD_SRC := $(SRC_DIR)/build.c
BUILD_OBJ := $(BIN_DIR)/build.o

FEATURES_SRC := $(SRC_DIR)/coffee_features.c
FEATURES_OBJ := $(BIN_DIR)/coffee_features.o

TOML_OBJ := $(BIN_DIR)/toml.o

OBJS := $(CORE_OBJ) $(COMMANDS_OBJ) $(MANIFEST_OBJ) $(REGISTRY_OBJ) $(PROJECT_OBJ) $(BUILD_OBJ) $(FEATURES_OBJ) $(TOML_OBJ) $(BIN_DIR)/cmdline.o

CFLAGS_COMMON := -g -Wall -Wextra -O3 -std=$(CSTD)
CFLAGS_COMMON += -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes
CFLAGS_COMMON += -pedantic -Wconversion -Wsign-conversion -Wunused -Wunused-function -Wunused-parameter
CFLAGS_COMMON += -Wfloat-equal -Wundef -Wmissing-declarations -Wmissing-include-dirs -Wmultichar -Wsystem-headers
CFLAGS_COMMON += -Wformat=2 -Wformat-security -Wnonnull
CFLAGS_COMMON += -D_GNU_SOURCE -I$(SRC_DIR) -I$(DEPS_DIR)
CFLAGS_COMMON += -fno-function-sections -fno-data-sections
CFLAGS_COMMON += -fasynchronous-unwind-tables -fno-common -fdebug-macro
CFLAGS_COMMON += -fno-delete-null-pointer-checks -fno-strict-overflow
CFLAGS_COMMON += -fno-strict-aliasing -fwrapv
CFLAGS_COMMON += -fno-omit-frame-pointer -fstack-protector-strong

LDFLAGS := -static -lz

CMDLINE_GEN := cmdline.c cmdline.h

DEPS_TOML_URL := https://raw.githubusercontent.com/cktan/tomlc99/master/toml.c
DEPS_TOML_H_URL := https://raw.githubusercontent.com/cktan/tomlc99/master/toml.h
DEPS_SDS_URL := https://raw.githubusercontent.com/antirez/sds/master/sds.h
DEPS_SDSALLOC_URL := https://raw.githubusercontent.com/antirez/sds/master/sdsalloc.h

TIDY := clang-tidy
TIDY_FLAGS = -- -std=$(CSTD) -D_GNU_SOURCE -I$(SRC_DIR) -I$(DEPS_DIR)
STAMP_DIR = .tidy_stamps
SRCS = $(wildcard $(SRC_DIR)/*.c)
STAMPS = $(patsubst $(SRC_DIR)/%.c, $(STAMP_DIR)/%.c.tidy, $(SRCS))

$(BIN_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) -c $< -o $@

$(BIN_DIR)/cmdline.o: $(SRC_DIR)/cmdline.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) -c $< -o $@

$(BIN_DIR)/toml.o: $(DEPS_DIR)/toml.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) -c $< -o $@

$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

all: format $(TARGET)

$(SRC_DIR)/cmdline.c $(SRC_DIR)/cmdline.h: $(SRC_DIR)/cli.ggo
	gengetopt -i $< --output-dir=$(SRC_DIR)/

bootstrap:
	@mkdir -p $(DEPS_DIR)/sds
	@echo "Checking dependencies..."
	@if [ ! -f $(DEPS_DIR)/toml.c ]; then \
		echo "Downloading toml.c..."; \
		curl -fsSL $(DEPS_TOML_URL) -o $(DEPS_DIR)/toml.c; \
	fi
	@if [ ! -f $(DEPS_DIR)/toml.h ]; then \
		echo "Downloading toml.h..."; \
		curl -fsSL $(DEPS_TOML_H_URL) -o $(DEPS_DIR)/toml.h; \
	fi
	@if [ ! -f $(DEPS_DIR)/sds/sds.h ]; then \
		echo "Downloading sds.h..."; \
		curl -fsSL $(DEPS_SDS_URL) -o $(DEPS_DIR)/sds/sds.h; \
	fi
	@if [ ! -f $(DEPS_DIR)/sds/sdsalloc.h ]; then \
		echo "Downloading sdsalloc.h..."; \
		curl -fsSL $(DEPS_SDSALLOC_URL) -o $(DEPS_DIR)/sds/sdsalloc.h; \
	fi
	@echo "Dependencies ready."

clean:
	rm -rf $(BIN_DIR)  $(STAMP_DIR)

format:
	clang-format -i $(SRC_DIR)/*.c $(SRC_DIR)/*.h $(SRC_DIR)/commands/*.c

tidy: $(STAMPS)

check: format tidy

test: $(TARGET)
	@failed=0; \
	for t in test_features test_makefile test_cflags_libs test_manifest_version test_registry_versions test_registry_fetch_versioned; do \
		if [ -n "$(TEST_FILTER)" ] && [ "$$t" != "$(TEST_FILTER)" ]; then \
			continue; \
		fi; \
		printf "  %-40s ... " "$$t"; \
		case $$t in \
		test_features) \
			clang -g -Wall -Wextra -O3 -std=$(CSTD) -I$(SRC_DIR) -I$(DEPS_DIR) \
				-o $(BIN_DIR)/$$t tests/$$t.c $(SRC_DIR)/manifest.c \
				$(SRC_DIR)/coffee_features.c $(DEPS_DIR)/toml.c -static -lz >/dev/null 2>&1 && \
			$(BIN_DIR)/$$t >/dev/null 2>&1; \
			;; \
		test_makefile) \
			clang -g -Wall -Wextra -O3 -std=$(CSTD) -I$(SRC_DIR) -I$(DEPS_DIR) \
				-o $(BIN_DIR)/$$t tests/$$t.c $(SRC_DIR)/manifest.c \
				$(DEPS_DIR)/toml.c -static -lz >/dev/null 2>&1 && \
			$(BIN_DIR)/$$t >/dev/null 2>&1; \
			;; \
		test_cflags_libs) \
			clang -g -Wall -Wextra -O3 -std=$(CSTD) \
				-o $(BIN_DIR)/$$t tests/$$t.c -static -lz >/dev/null 2>&1 && \
			$(BIN_DIR)/$$t >/dev/null 2>&1; \
			;; \
		test_manifest_version) \
			clang -g -Wall -Wextra -O3 -std=$(CSTD) -I$(SRC_DIR) -I$(DEPS_DIR) \
				-o $(BIN_DIR)/$$t tests/$$t.c $(SRC_DIR)/manifest.c \
				$(DEPS_DIR)/toml.c -static -lz >/dev/null 2>&1 && \
			$(BIN_DIR)/$$t >/dev/null 2>&1; \
			;; \
		test_registry_versions) \
			clang -g -Wall -Wextra -O3 -std=$(CSTD) -D_GNU_SOURCE -I$(SRC_DIR) -I$(DEPS_DIR) \
				-o $(BIN_DIR)/$$t tests/$$t.c $(SRC_DIR)/registry.c -static -lz >/dev/null 2>&1 && \
			$(BIN_DIR)/$$t >/dev/null 2>&1; \
			;; \
		test_registry_fetch_versioned) \
			clang -g -Wall -Wextra -O3 -std=$(CSTD) -D_GNU_SOURCE -I$(SRC_DIR) -I$(DEPS_DIR) \
				-o $(BIN_DIR)/$$t tests/$$t.c $(SRC_DIR)/registry.c -static -lz >/dev/null 2>&1 && \
			$(BIN_DIR)/$$t >/dev/null 2>&1; \
			;; \
		esac; \
		rc=$$?; \
		if [ $$rc -ne 0 ]; then \
			echo "FAIL"; \
			failed=1; \
		else \
			echo "PASS"; \
		fi; \
	done; \
	if [ $$failed -ne 0 ]; then \
		exit 1; \
	fi; \
	echo "All tests passed."

.PHONY: clean format tidy check bootstrap test

# Create the stamp directory
$(STAMP_DIR):
	mkdir -p $(STAMP_DIR)

# The Linting Rule
# Note: This will now re-run if the .c file OR any included .h file changes
$(STAMP_DIR)/%.c.tidy: $(SRC_DIR)/%.c | $(STAMP_DIR)
	@echo "Linting $<..."
	@$(TIDY) $< $(TIDY_FLAGS)
	@touch $@


