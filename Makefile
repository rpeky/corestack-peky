CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -g3 -O0 -Iinclude -D_GNU_SOURCE

BIN_DIR := bin
OBJ_DIR := build/obj

TETRISH_SRCS := $(wildcard src/tetrish/*.c)
TETRISH_OBJS := $(TETRISH_SRCS:%.c=$(OBJ_DIR)/%.o)

SYS_SRCS := $(wildcard src/tetrish/system_programs/*.c)
SYS_BINS := $(patsubst src/tetrish/system_programs/%.c,$(BIN_DIR)/%,$(SYS_SRCS))

TARGETS := $(BIN_DIR)/tetrish $(SYS_BINS)

.PHONY: all clean run bear fmt

all: $(TARGETS)

$(BIN_DIR)/tetrish: $(TETRISH_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $^ -o $@

$(BIN_DIR)/%: src/tetrish/system_programs/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

run: $(BIN_DIR)/tetrish
	./$(BIN_DIR)/tetrish

bear: clean
	bear -- $(MAKE) all

fmt:
	clang-format -i $$(find src include -name '*.c' -o -name '*.h')

clean:
	rm -rf build
	rm -rf bin


-include $(TETRISH_OBJS:.o=.d)
