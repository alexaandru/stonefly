# Stonefly root Makefile — run from this directory.
ROOT := $(CURDIR)
# aports-style: dist/testing/stonefly → repo name "testing" under REPODEST
# (default REPODEST is often /home/apk → /home/apk/testing/x86_64/).
APORT := dist/alpine
PKGVER := 0.0.1
TARBALL := $(APORT)/stonefly-$(PKGVER).tar.gz

# Meson build dir with compile_commands.json (abuild uses "output").
BUILDDIR ?= output

# Directory mtimes do not bump when a file inside src/ changes — depend on
# every packed path so `make apk` cannot ship a stale tree.
SRC_FILES := $(shell find src protocol runtime tests -type f \( -name '*.c' -o -name '*.h' -o -name '*.xml' -o -name '*.lua' -o -name '*.fnl' -o -name 'meson.build' \) 2>/dev/null | sort)
STYLE_FILES := .clang-format .clang-tidy .cppcheck-suppress
PACK_META := meson.build Makefile LICENSE LICENSE.tinywl README.md FEATURES.md ROADMAP.md AGENTS.md $(STYLE_FILES)

C_SOURCES := $(shell find src -type f -name '*.c' | sort)
H_SOURCES := $(shell find src -type f -name '*.h' | sort)
TEST_C_SOURCES := $(shell find tests -type f -name '*.c' | sort)
TEST_H_SOURCES := $(shell find tests -type f -name '*.h' | sort)
FMT_SOURCES := $(C_SOURCES) $(H_SOURCES) $(TEST_C_SOURCES) $(TEST_H_SOURCES)

# Host unit-test binaries (no wlroots / Meson configure). abuild still uses
# meson test against BUILDDIR after build().
# UNITDIR=build/unit (default) or build/unit-cover (OPTS=cover via tests/run.sh).
CC ?= cc
CFLAGS_TEST ?= -std=c23 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L
UNITDIR ?= build/unit
ifeq ($(UNITDIR),build/unit-cover)
COVFLAGS := --coverage
else
COVFLAGS :=
endif

.PHONY: apk dist-tarball clean fmt fmt-check cppcheck tidy style-check test coverage

dist-tarball: $(TARBALL)

$(TARBALL): $(PACK_META) $(SRC_FILES)
	mkdir -p "$(APORT)"
	tar -C "$(ROOT)" -czf "$(TARBALL)" \
		--transform 's,^,stonefly-$(PKGVER)/,' \
		meson.build Makefile src protocol runtime tests \
		LICENSE LICENSE.tinywl README.md FEATURES.md ROADMAP.md AGENTS.md \
		.clang-format .clang-tidy .cppcheck-suppress

# checksum + build; APK lands under $$REPODEST/testing/x86_64/.
apk: fmt $(TARBALL)
	cd "$(APORT)" && abuild checksum && abuild -r 2>&1 | tee build.log

# --- style / static analysis (tools from abuild checkdepends) ---
# Quiet by default: one status line each; findings/errors still print.

fmt:
	clang-format -i $(FMT_SOURCES)

fmt-check:
	@printf 'fmt-check\n'
	@clang-format --dry-run --Werror $(FMT_SOURCES)

cppcheck:
	@printf 'cppcheck\n'
	@cppcheck -q --project="$(BUILDDIR)/compile_commands.json" \
		--suppressions-list=.cppcheck-suppress \
		--error-exitcode=1 \
		--inline-suppr

tidy:
	@printf 'clang-tidy\n'
	@clang-tidy -p "$(BUILDDIR)" --quiet $(C_SOURCES)

style-check: fmt-check cppcheck tidy

# Go-style runner: OPTS=verbose,cover (comma-separated). See tests/run.sh.
# Default OPTS is cover; override with OPTS= / OPTS=verbose / OPTS=verbose,cover.
OPTS ?= cover

test:
	@OPTS="$(OPTS)" CC="$(CC)" CFLAGS_TEST="$(CFLAGS_TEST)" ./tests/run.sh

# Alias for make test (cover is already the default).
coverage: test

# --- unit objects / binaries (rebuild only when inputs change) ---

$(UNITDIR)/sf_test.o: tests/sf_test.c tests/sf_test.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests -I src -c tests/sf_test.c -o $@

$(UNITDIR)/log_stub.o: tests/stubs/log_stub.c src/log/log.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests -I src -c tests/stubs/log_stub.c -o $@

$(UNITDIR)/math_test.o: tests/util/math_test.c tests/sf_test.h src/util/math.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests -I src -c tests/util/math_test.c -o $@

$(UNITDIR)/color_test.o: tests/util/color_test.c tests/sf_test.h src/util/color.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests -I src -c tests/util/color_test.c -o $@

$(UNITDIR)/time_test.o: tests/util/time_test.c tests/sf_test.h src/util/time.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests -I src -c tests/util/time_test.c -o $@

$(UNITDIR)/str_test.o: tests/util/str_test.c tests/sf_test.h src/util/str.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests -I src -c tests/util/str_test.c -o $@

$(UNITDIR)/json_test.o: tests/util/json_test.c tests/sf_test.h src/util/json.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests -I src -c tests/util/json_test.c -o $@

$(UNITDIR)/str.o: src/util/str.c src/util/str.h src/log/log.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests -I src -c src/util/str.c -o $@

$(UNITDIR)/json.o: src/util/json.c src/util/json.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests -I src -c src/util/json.c -o $@

$(UNITDIR)/util_math_test: $(UNITDIR)/sf_test.o $(UNITDIR)/math_test.o
	$(CC) $(COVFLAGS) -o $@ $^ -lm

$(UNITDIR)/util_color_test: $(UNITDIR)/sf_test.o $(UNITDIR)/color_test.o
	$(CC) $(COVFLAGS) -o $@ $^ -lm

$(UNITDIR)/util_time_test: $(UNITDIR)/sf_test.o $(UNITDIR)/time_test.o
	$(CC) $(COVFLAGS) -o $@ $^

$(UNITDIR)/util_str_test: $(UNITDIR)/sf_test.o $(UNITDIR)/str_test.o \
		$(UNITDIR)/log_stub.o $(UNITDIR)/str.o
	$(CC) $(COVFLAGS) -o $@ $^

$(UNITDIR)/util_json_test: $(UNITDIR)/sf_test.o $(UNITDIR)/json_test.o $(UNITDIR)/json.o
	$(CC) $(COVFLAGS) -o $@ $^

clean:
	rm -f "$(TARBALL)"
	rm -rf "$(APORT)/src" "$(APORT)/pkg" build/unit build/unit-cover build/coverage
