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

clean:
	rm -rf $(BIN_DIR)

indent:
	find . -name "*.c" -o -name "*.h" | xargs clang-format -i -style=file

format:
	clang-format -i $(SRC_DIR)/*.c $(SRC_DIR)/*.h $(SRC_DIR)/commands/*.c

.PHONY: clean indent format tidy check

tidy:
	clang-tidy $(SRC_DIR)/*.c $(SRC_DIR)/commands/*.c \
		-- $(CFLAGS_COMMON) -I$(SRC_DIR) -I$(DEPS_DIR)

check: format tidy

.PHONY: clean indent format tidy check
