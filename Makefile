.DEFAULT_GOAL := all
.PHONY: all clean test account-test storage-test integration-test staff-integration-test storage-integration-test soak-test check sanitize

CC ?= cc
CFLAGS ?= -O2
CPPFLAGS ?=
WARNINGS ?= -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes
LDFLAGS ?=
LDLIBS ?=
PKG_CONFIG ?= pkg-config
ACCOUNT_FLAGS = $(shell $(PKG_CONFIG) --cflags libsodium sqlite3)
ACCOUNT_LIBS = $(shell $(PKG_CONFIG) --libs libsodium sqlite3) -pthread

# Keep the required language dialect when callers override optimization flags.
override CFLAGS += -std=c99

all: glad glad-admin

glad-admin: account_admin.c accounts.c accounts.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(ACCOUNT_FLAGS) $(LDFLAGS) -o $@ account_admin.c accounts.c $(LDLIBS) $(ACCOUNT_LIBS)

account-tests: account_tests.c accounts.c accounts.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(ACCOUNT_FLAGS) $(LDFLAGS) -o $@ account_tests.c accounts.c $(LDLIBS) $(ACCOUNT_LIBS)

account-test: account-tests
	@mkdir -p .account-test-run
	@cd .account-test-run && ../account-tests

storage-tests: storage_tests.c player_storage.c player_storage.h player_record.c player_record.h
	$(CC) $(CPPFLAGS) -DGLAD_STORAGE_TESTING $(CFLAGS) $(WARNINGS) $(ACCOUNT_FLAGS) $(LDFLAGS) -o $@ storage_tests.c player_storage.c player_record.c $(LDLIBS) $(ACCOUNT_LIBS)

storage-test: storage-tests
	@mkdir -p .storage-test-run
	@cd .storage-test-run && ../storage-tests

GAME_SOURCES = glad.c commands.c account_commands.c staff_commands.c telnet.c accounts.c accounts.h player_record.c player_record.h player_storage.c player_storage.h

glad: $(GAME_SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(ACCOUNT_FLAGS) $(LDFLAGS) -o $@ glad.c accounts.c player_record.c player_storage.c $(LDLIBS) $(ACCOUNT_LIBS)

glad-tests: tests.c $(GAME_SOURCES)
	$(CC) $(CPPFLAGS) -UPORT -DPORT=0 -DGLAD_STORAGE_TESTING $(CFLAGS) $(WARNINGS) $(ACCOUNT_FLAGS) $(LDFLAGS) -o $@ tests.c accounts.c player_record.c player_storage.c $(LDLIBS) $(ACCOUNT_LIBS)

glad-smoke: $(GAME_SOURCES)
	$(CC) $(CPPFLAGS) -UPORT -DPORT=0 $(CFLAGS) $(WARNINGS) $(ACCOUNT_FLAGS) $(LDFLAGS) -o $@ glad.c accounts.c player_record.c player_storage.c $(LDLIBS) $(ACCOUNT_LIBS)

test: glad-tests
	@mkdir -p .test-run
	@cd .test-run && ../glad-tests

integration-test: glad-smoke glad-admin
	python3 tests/smoke.py ./glad-smoke ./glad-admin

staff-integration-test: glad-smoke glad-admin
	python3 tests/staff_smoke.py ./glad-smoke ./glad-admin

storage-integration-test: glad-smoke glad-admin
	python3 tests/storage_smoke.py ./glad-smoke ./glad-admin

# Opt-in only: bounded real-time exercise, not part of each required CI job.
SOAK_SECONDS ?= 300
soak-test: glad-smoke glad-admin
	python3 tests/soak.py ./glad-smoke ./glad-admin --seconds $(SOAK_SECONDS)

check: test account-test storage-test integration-test staff-integration-test storage-integration-test

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS="-O1 -g -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer" check

clean:
	rm -f glad glad-tests glad-smoke glad-admin account-tests storage-tests
	rm -rf .test-run .account-test-run .storage-test-run
