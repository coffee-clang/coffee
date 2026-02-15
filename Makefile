SHELL := bash
.SHELLSFLAGS := -eu -o pipefail -c

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

TOML_OBJ := $(BIN_DIR)/toml.o

OBJS := $(CORE_OBJ) $(COMMANDS_OBJ) $(MANIFEST_OBJ) $(REGISTRY_OBJ) $(PROJECT_OBJ) $(BUILD_OBJ) $(TOML_OBJ) $(BIN_DIR)/cmdline.o

CFLAGS_COMMON := -g -Wall -Wextra -O2 -std=$(CSTD)
CFLAGS_COMMON += -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes
CFLAGS_COMMON += -D_GNU_SOURCE
CFLAGS_COMMON += -I$(SRC_DIR) -I$(DEPS_DIR)

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

all: indent $(TARGET)

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

indent:
	find . -name "*.c" -o -name "*.h" | xargs clang-format -i -style=file

format:
	clang-format -i $(SRC_DIR)/*.c $(SRC_DIR)/*.h $(SRC_DIR)/commands/*.c

.PHONY: clean indent format tidy check bootstrap

check: format  $(STAMPS)

.PHONY: clean indent format tidy check bootstrap

# Create the stamp directory
$(STAMP_DIR):
	mkdir -p $(STAMP_DIR)

# The Linting Rule
# Note: This will now re-run if the .c file OR any included .h file changes
$(STAMP_DIR)/%.c.tidy: $(SRC_DIR)/%.c | $(STAMP_DIR)
	@echo "Linting $<..."
	@$(TIDY) $< $(TIDY_FLAGS)
	@touch $@

# Advanced: Header Dependency Integration
# If you have an existing build process generating .d files,
# you can include them here so header changes trigger a re-lint.
-include $(SRCS:$(SRC_DIR)/%.c=build/%.d)
