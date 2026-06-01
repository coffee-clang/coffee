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

SDS_OBJ := $(BIN_DIR)/sds.o

OBJS := $(CORE_OBJ) $(COMMANDS_OBJ) $(MANIFEST_OBJ) $(REGISTRY_OBJ) $(PROJECT_OBJ) $(BUILD_OBJ) $(FEATURES_OBJ) $(TOML_OBJ) $(SDS_OBJ) $(BIN_DIR)/cmdline.o

CFLAGS_COMMON := -g -Wall -Wextra -O3 -std=$(CSTD)
CFLAGS_COMMON += -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes
CFLAGS_COMMON += -pedantic -Wconversion -Wsign-conversion -Wunused -Wunused-function -Wunused-parameter
CFLAGS_COMMON += -Wfloat-equal -Wundef -Wmissing-declarations -Wmissing-include-dirs -Wmultichar -Wsystem-headers
CFLAGS_COMMON += -Wformat=2 -Wformat-security -Wnonnull
CFLAGS_COMMON += -D_GNU_SOURCE -I$(SRC_DIR) -isystem$(DEPS_DIR) -isystem$(DEPS_DIR)/toml
CFLAGS_COMMON += -fno-function-sections -fno-data-sections
CFLAGS_COMMON += -fasynchronous-unwind-tables -fno-common -fdebug-macro
CFLAGS_COMMON += -fno-delete-null-pointer-checks -fno-strict-overflow
CFLAGS_COMMON += -fno-strict-aliasing -fwrapv
CFLAGS_COMMON += -fno-omit-frame-pointer -fstack-protector-strong

LDFLAGS := -static -lz

DEPS_TOML_URL := https://raw.githubusercontent.com/cktan/tomlc99/master/toml.c
DEPS_TOML_H_URL := https://raw.githubusercontent.com/cktan/tomlc99/master/toml.h
DEPS_SDS_URL := https://raw.githubusercontent.com/antirez/sds/master/sds.h
DEPS_SDSALLOC_URL := https://raw.githubusercontent.com/antirez/sds/master/sdsalloc.h
DEPS_SDS_C_URL := https://raw.githubusercontent.com/antirez/sds/master/sds.c

MDBOOK := $(if $(wildcard ./mdbook),./mdbook,mdbook)

TIDY := clang-tidy
TIDY_FLAGS = -- -std=$(CSTD) -D_GNU_SOURCE -I$(SRC_DIR) -isystem$(DEPS_DIR) -isystem$(DEPS_DIR)/toml
STAMP_DIR = .tidy_stamps
SRCS = $(filter-out $(SRC_DIR)/cmdline.c, $(wildcard $(SRC_DIR)/*.c))
COMMANDS_SRCS = $(wildcard $(SRC_DIR)/commands/*.c)
ALL_SRCS = $(SRCS) $(COMMANDS_SRCS)
STAMPS = $(patsubst $(SRC_DIR)/%.c, $(STAMP_DIR)/%.c.tidy, $(ALL_SRCS))

# Test runner
TEST_SRCS := $(wildcard tests/*.c)
TEST_OBJS := $(TEST_SRCS:tests/%.c=$(BIN_DIR)/tests/%.o)

# All support objects for the test runner (everything except coffee.o which has main())
TEST_SUPPORT_OBJS := $(filter-out $(CORE_OBJ), $(COMMANDS_OBJ) $(MANIFEST_OBJ) $(REGISTRY_OBJ) $(PROJECT_OBJ) $(BUILD_OBJ) $(FEATURES_OBJ) $(TOML_OBJ) $(SDS_OBJ) $(BIN_DIR)/cmdline.o)

$(BIN_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) -c $< -o $@

$(BIN_DIR)/cmdline.o: $(SRC_DIR)/cmdline.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) -c $< -o $@

$(BIN_DIR)/toml.o: $(DEPS_DIR)/toml/toml.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) -c $< -o $@

$(BIN_DIR)/sds.o: $(DEPS_DIR)/sds/sds.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) -c $< -o $@

# Test object files
$(BIN_DIR)/tests/%.o: tests/%.c tests/test_framework.h
	@mkdir -p $(BIN_DIR)/tests
	$(CC) $(CFLAGS_COMMON) -Itests -c $< -o $@

# coffee.o for test runner (without main())
$(BIN_DIR)/tests/coffee_test_runner.o: $(SRC_DIR)/coffee.c
	@mkdir -p $(BIN_DIR)/tests
	$(CC) $(CFLAGS_COMMON) -DCOFFEE_TEST_RUNNER -c $< -o $@

# Test runner binary
$(BIN_DIR)/tests/runner: $(TEST_OBJS) $(TEST_SUPPORT_OBJS) $(BIN_DIR)/tests/coffee_test_runner.o
	@mkdir -p $(BIN_DIR)/tests
	$(CC) $(LDFLAGS) -o $@ $^

$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

all: format $(TARGET) docs

bootstrap:
	@mkdir -p $(DEPS_DIR)/sds $(DEPS_DIR)/toml
	@echo "Checking dependencies..."
	@if [ ! -f $(DEPS_DIR)/toml/toml.c ]; then \
		echo "Downloading toml/toml.c..."; \
		curl -fsSL $(DEPS_TOML_URL) -o $(DEPS_DIR)/toml/toml.c; \
	fi
	@if [ ! -f $(DEPS_DIR)/toml/toml.h ]; then \
		echo "Downloading toml/toml.h..."; \
		curl -fsSL $(DEPS_TOML_H_URL) -o $(DEPS_DIR)/toml/toml.h; \
	fi
	@if [ ! -f $(DEPS_DIR)/sds/sds.h ]; then \
		echo "Downloading sds.h..."; \
		curl -fsSL $(DEPS_SDS_URL) -o $(DEPS_DIR)/sds/sds.h; \
	fi
	@if [ ! -f $(DEPS_DIR)/sds/sdsalloc.h ]; then \
		echo "Downloading sdsalloc.h..."; \
		curl -fsSL $(DEPS_SDSALLOC_URL) -o $(DEPS_DIR)/sds/sdsalloc.h; \
	fi
	@if [ ! -f $(DEPS_DIR)/sds/sds.c ]; then \
		echo "Downloading sds.c..."; \
		curl -fsSL $(DEPS_SDS_C_URL) -o $(DEPS_DIR)/sds/sds.c; \
	fi
	@echo "Dependencies ready."

clean:
	rm -rf $(BIN_DIR) $(STAMP_DIR)

format:
	clang-format -i $(SRC_DIR)/*.c $(SRC_DIR)/*.h $(SRC_DIR)/commands/*.c tests/*.c tests/*.h

tidy: $(STAMPS)

check: format tidy

test: $(TARGET) $(BIN_DIR)/tests/runner
	@if [ -n "$(TEST_FILTER)" ]; then \
		$(BIN_DIR)/tests/runner "$(TEST_FILTER)"; \
	else \
		$(BIN_DIR)/tests/runner; \
	fi

# Install
INSTALL_DIR ?= $(HOME)/.coffee/bin

install: $(TARGET)
	@mkdir -p $(INSTALL_DIR)
	cp $(TARGET) $(INSTALL_DIR)/$(P)
	@echo "Installed $(P) to $(INSTALL_DIR)/$(P)"

# Benchmark targets
BENCH_SRCS := $(wildcard bench/*.c)
BENCH_BINS := $(patsubst bench/%.c, $(BIN_DIR)/bench/%, $(BENCH_SRCS))

$(BIN_DIR)/bench/%: bench/%.c $(TARGET)
	@mkdir -p $(BIN_DIR)/bench
	$(CC) $(CFLAGS_COMMON) $(INC_FLAGS) -o $@ $<

bench: $(BENCH_BINS)
	@echo "Running benchmarks..."
	@for b in $(BENCH_BINS); do \
		echo "=== $$(basename $$b) ==="; \
		time ./$$b; \
		echo; \
	done

.PHONY: clean format tidy check bootstrap test docs-assets docs serve install

docs-assets:
	@echo "Fetching remote docs theme assets..."
	@./scripts/fetch-docs-assets.sh

docs: docs-assets
	@echo "Building docs website..."
	@$(MDBOOK) build
	@echo "Docs built to book/"

serve: docs-assets
	$(MDBOOK) serve

$(STAMP_DIR):
	mkdir -p $(STAMP_DIR)

# The Linting Rule
# Note: This will now re-run if the .c file OR any included .h file changes
$(STAMP_DIR)/%.c.tidy: $(SRC_DIR)/%.c | $(STAMP_DIR)
	@mkdir -p $(dir $@)
	@echo "Linting $<..."
	@$(TIDY) --quiet $< $(TIDY_FLAGS)
	@touch $@


