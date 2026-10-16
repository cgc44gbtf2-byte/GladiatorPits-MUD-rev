.DEFAULT_GOAL := all
.PHONY: all clean test account-test integration-test check sanitize

CC ?= cc
CFLAGS ?= -O2
CPPFLAGS ?=
WARNINGS ?= -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes
LDFLAGS ?=
LDLIBS ?=
PKG_CONFIG ?= pkg-config
ACCOUNT_FLAGS = $(shell $(PKG_CONFIG) --cflags libsodium sqlite3)
ACCOUNT_LIBS = $(shell $(PKG_CONFIG) --libs libsodium sqlite3) -pthread

CFLAGS += -std=c99

all: glad glad-admin

glad-admin: account_admin.c accounts.c accounts.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(ACCOUNT_FLAGS) $(LDFLAGS) -o $@ account_admin.c accounts.c $(LDLIBS) $(ACCOUNT_LIBS)

account-tests: account_tests.c accounts.c accounts.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(ACCOUNT_FLAGS) $(LDFLAGS) -o $@ account_tests.c accounts.c $(LDLIBS) $(ACCOUNT_LIBS)

account-test: account-tests
	@mkdir -p .account-test-run
	@cd .account-test-run && ../account-tests

GAME_SOURCES = glad.c commands.c account_commands.c accounts.c accounts.h

glad: $(GAME_SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(ACCOUNT_FLAGS) $(LDFLAGS) -o $@ glad.c accounts.c $(LDLIBS) $(ACCOUNT_LIBS)

glad-tests: tests.c $(GAME_SOURCES)
	$(CC) $(CPPFLAGS) -UPORT -DPORT=0 $(CFLAGS) $(WARNINGS) $(ACCOUNT_FLAGS) $(LDFLAGS) -o $@ tests.c accounts.c $(LDLIBS) $(ACCOUNT_LIBS)

glad-smoke: $(GAME_SOURCES)
	$(CC) $(CPPFLAGS) -UPORT -DPORT=0 $(CFLAGS) $(WARNINGS) $(ACCOUNT_FLAGS) $(LDFLAGS) -o $@ glad.c accounts.c $(LDLIBS) $(ACCOUNT_LIBS)

test: glad-tests
	@mkdir -p .test-run
	@cd .test-run && ../glad-tests

integration-test: glad-smoke glad-admin
	python3 tests/smoke.py ./glad-smoke ./glad-admin

check: test account-test integration-test

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS="-O1 -g -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer" check

clean:
	rm -f glad glad-tests glad-smoke glad-admin account-tests
	rm -rf .test-run .account-test-run
