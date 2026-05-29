SHELL := bash
.SHELLFLAGS := -eu -o pipefail -c
P = coffee

STD_LIBS = zlib

COMMANDS_SRC = $(wildcard commands/*.c)
COMMANDS_OBJ = ${COMMANDS_SRC:.c=.o}
$(info    COMMANDS_SRC is $(COMMANDS_SRC))
$(info    COMMANDS_OBJ is $(COMMANDS_OBJ))


OBJS 	= cmdline.o $(COMMANDS_OBJ)

CFLAGS_STD = -g -Wall -DDEBUG -O3 -march=native -Wno-deprecated -Wno-parentheses -Wno-format
CFLAGS_EXTRA =  -m64 -std=c23 -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes

CFLAGS_LIBS = `pkg-config --cflags $(STD_LIBS)`
CFLAGS_TEST =  -DTEST_EVERYTHING  `pkg-config --cflags $(DEBUG_LIBS)`
LDLIBS = `pkg-config --libs $(STD_LIBS)`
CFLAGS = $(CFLAGS_STD) $(CFLAGS_EXTRA) $(CFLAGS_LIB)

CC_DEPS = $(CC) $(CFLAGS) -c       -I$(SRC_DIR) -I$(LIB_DIR) $(CFLAGS_LIBS)
CC_FULL = $(CC) $(CFLAGS) -static -I$(SRC_DIR) -I$(LIB_DIR) $(CFLAGS_LIBS)



$(P): $(P).c $(P).h $(OBJS)

commands/%.o: commands/%.c $(P).h
	$(CC_DEPS)$< -o $@


clean:
	@echo "Cleaning..."
	rm -rf *.o *.d $(P) cmdline.* tests/output/*

#test: run-tests.sh $(P)
#	./run-tests.sh

.PHONY: clean

ifneq "$(MAKECMDGOALS)" "clean"
-include ${SOURCES:.c=.d}
-include ${T_SOURCES:.c=.d}
endif

