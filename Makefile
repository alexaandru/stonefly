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
SRC_FILES := $(shell find src protocol runtime tests -type f \( -name '*.c' -o -name '*.h' -o -name '*.py' -o -name '*.sh' -o -name '*.xml' -o -name '*.lua' -o -name '*.fnl' -o -name 'meson.build' \) 2>/dev/null | sort)
STYLE_FILES := .clang-format .clang-tidy .cppcheck-suppress
PACK_META := meson.build Makefile LICENSE LICENSE.tinywl README.md FEATURES.md ROADMAP.md AGENTS.md $(STYLE_FILES)

C_SOURCES := $(shell find src -type f -name '*.c' | sort)
H_SOURCES := $(shell find src -type f -name '*.h' | sort)
TEST_C_SOURCES := $(shell find tests -type f -name '*.c' | sort)
TEST_H_SOURCES := $(shell find tests -type f -name '*.h' | sort)
FMT_SOURCES := $(C_SOURCES) $(H_SOURCES) $(TEST_C_SOURCES) $(TEST_H_SOURCES)


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

# Unit tests: tests/drive.py discovers tests/**/*_test.c via /* sf-suite … */.
# Makefile only forwards OPTS/CC — no per-suite object lists.
CC ?= cc
CFLAGS_TEST ?= -std=c23 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L

# Go-style runner: OPTS=verbose,cover (comma-separated). Default cover.
OPTS ?= cover

test:
	@OPTS="$(OPTS)" CC="$(CC)" CFLAGS_TEST="$(CFLAGS_TEST)" ./tests/drive.py

# Alias for make test (cover is already the default).
coverage: test

clean:
	rm -f "$(TARBALL)"
	rm -rf "$(APORT)/src" "$(APORT)/pkg" build/unit build/unit-cover build/coverage
