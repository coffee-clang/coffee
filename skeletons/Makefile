CC ?= clang
CFLAGS += -std=c23 -O3 -g
CFLAGS += -Wall -Wextra -Wshadow -Wpedantic
CFLAGS += -Wconversion -Wsign-conversion -Wunused
CFLAGS += -Iinclude/__NAME__

# Dependency flags via coffee
CFLAGS += $(shell coffee cflags 2>/dev/null)
LDFLAGS += $(shell coffee libs 2>/dev/null)

TARGET := build/__NAME__
SOURCES := $(wildcard src/*.c)

.PHONY: build clean format tidy

build: $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)

clean:
	rm -rf build/

format:
	clang-format -i src/*.c include/__NAME__/*.h

tidy:
	clang-tidy src/*.c -- $(CFLAGS)
