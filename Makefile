# minishell - build, test and packaging rules.
#
#   make            build bin/minishell
#   make test       build and run the unit and integration suites
#   make debug      build with sanitizers and no optimisation
#   make clean      remove build products

CC       ?= gcc
CFLAGS   ?= -O2
CFLAGS   += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wshadow -Wwrite-strings \
            -Wno-unused-parameter -Iinclude
LDLIBS   ?=

# iMan fetches over HTTPS when OpenSSL is available, and falls back to the
# locally installed man(1) when it is not. Neither path is required to build.
ifeq ($(shell pkg-config --exists openssl 2>/dev/null && echo yes),yes)
CFLAGS += -DHAVE_OPENSSL $(shell pkg-config --cflags openssl)
LDLIBS += $(shell pkg-config --libs openssl)
endif

BIN_DIR   := bin
BUILD_DIR := build
TARGET    := $(BIN_DIR)/minishell

SRCS      := $(wildcard src/*.c) $(wildcard src/builtins/*.c)
OBJS      := $(SRCS:%.c=$(BUILD_DIR)/%.o)
LIB_OBJS  := $(filter-out $(BUILD_DIR)/src/main.o,$(OBJS))
DEPS      := $(OBJS:.o=.d)

TEST_SRCS := $(wildcard tests/unit/*.c)
TEST_OBJS := $(TEST_SRCS:%.c=$(BUILD_DIR)/%.o)
TEST_BIN  := $(BIN_DIR)/unit-tests

.PHONY: all clean test unit integration interactive debug run help

all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_BIN): $(TEST_OBJS) $(LIB_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

# Sanitizers catch the memory and job-control mistakes that a shell invites.
debug:
	$(MAKE) clean
	$(MAKE) CFLAGS="-std=c11 -D_GNU_SOURCE -Wall -Wextra -Wshadow -Iinclude -g3 -O0 \
	                -fsanitize=address,undefined -fno-omit-frame-pointer"

test: unit integration interactive

unit: $(TEST_BIN)
	@./$(TEST_BIN)

integration: $(TARGET)
	@./tests/integration.sh

# The line editor and job control only exist on a terminal, so these drive the
# shell through a pty. Skipped, not failed, when python3 is unavailable.
interactive: $(TARGET)
	@if command -v python3 >/dev/null 2>&1; then \
	    python3 ./tests/interactive.py; \
	 else \
	    echo "interactive: python3 not found, skipping"; \
	 fi

run: $(TARGET)
	@./$(TARGET)

clean:
	$(RM) -r $(BUILD_DIR) $(BIN_DIR)

help:
	@echo "targets: all test unit integration interactive debug run clean"

-include $(DEPS)
