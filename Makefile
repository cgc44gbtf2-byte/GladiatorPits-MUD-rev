.DEFAULT_GOAL := all
.PHONY: all clean test integration-test check sanitize

CC ?= cc
CFLAGS ?= -O2
CPPFLAGS ?=
WARNINGS ?= -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes
LDFLAGS ?=
LDLIBS ?=

CFLAGS += -std=c99

all: glad

glad: glad.c commands.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(LDFLAGS) -o $@ glad.c $(LDLIBS)

glad-tests: tests.c glad.c commands.c
	$(CC) $(CPPFLAGS) -UPORT -DPORT=0 $(CFLAGS) $(WARNINGS) $(LDFLAGS) -o $@ tests.c $(LDLIBS)

glad-smoke: glad.c commands.c
	$(CC) $(CPPFLAGS) -UPORT -DPORT=0 $(CFLAGS) $(WARNINGS) $(LDFLAGS) -o $@ glad.c $(LDLIBS)

test: glad-tests
	@mkdir -p .test-run
	@cd .test-run && ../glad-tests

integration-test: glad-smoke
	python3 tests/smoke.py ./glad-smoke

check: test integration-test

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS="-O1 -g -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer" check

clean:
	rm -f glad glad-tests glad-smoke
	rm -rf .test-run
