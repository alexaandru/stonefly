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
# Host builds use tests/stubs/include ahead of system headers (no wlroots on host).

$(UNITDIR)/sf_test.o: tests/sf_test.c tests/sf_test.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c tests/sf_test.c -o $@

$(UNITDIR)/log_stub.o: tests/stubs/log_stub.c src/log/log.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c tests/stubs/log_stub.c -o $@

$(UNITDIR)/math_test.o: tests/util/math_test.c tests/sf_test.h src/util/math.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c tests/util/math_test.c -o $@

$(UNITDIR)/color_test.o: tests/util/color_test.c tests/sf_test.h src/util/color.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c tests/util/color_test.c -o $@

$(UNITDIR)/time_test.o: tests/util/time_test.c tests/sf_test.h src/util/time.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c tests/util/time_test.c -o $@

$(UNITDIR)/str_test.o: tests/util/str_test.c tests/sf_test.h src/util/str.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c tests/util/str_test.c -o $@

$(UNITDIR)/json_test.o: tests/util/json_test.c tests/sf_test.h src/util/json.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c tests/util/json_test.c -o $@

$(UNITDIR)/ssd_layout_test.o: tests/ssd/layout_test.c tests/sf_test.h src/ssd/ssd_layout.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c tests/ssd/layout_test.c -o $@

$(UNITDIR)/ssd_policy_test.o: tests/ssd/policy_test.c tests/sf_test.h src/ssd/ssd_policy.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c tests/ssd/policy_test.c -o $@

$(UNITDIR)/config_action_test.o: tests/config/action_test.c tests/sf_test.h src/config/action.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c tests/config/action_test.c -o $@

$(UNITDIR)/ipc_windows_json_test.o: tests/ipc/windows_json_test.c tests/sf_test.h src/ipc/windows.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c tests/ipc/windows_json_test.c -o $@

$(UNITDIR)/ipc_outputs_json_test.o: tests/ipc/outputs_json_test.c tests/sf_test.h src/ipc/outputs.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c tests/ipc/outputs_json_test.c -o $@

$(UNITDIR)/ipc_workspaces_json_test.o: tests/ipc/workspaces_json_test.c tests/sf_test.h src/ipc/workspaces.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c tests/ipc/workspaces_json_test.c -o $@

$(UNITDIR)/ipc_layers_json_test.o: tests/ipc/layers_json_test.c tests/sf_test.h src/ipc/layers.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c tests/ipc/layers_json_test.c -o $@

$(UNITDIR)/str.o: src/util/str.c src/util/str.h src/log/log.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c src/util/str.c -o $@

$(UNITDIR)/json.o: src/util/json.c src/util/json.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c src/util/json.c -o $@

$(UNITDIR)/ssd_layout.o: src/ssd/ssd_layout.c src/ssd/ssd_layout.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c src/ssd/ssd_layout.c -o $@

$(UNITDIR)/ssd_policy.o: src/ssd/ssd_policy.c src/ssd/ssd_policy.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c src/ssd/ssd_policy.c -o $@

$(UNITDIR)/action.o: src/config/action.c src/config/action.h src/util/str.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c src/config/action.c -o $@

$(UNITDIR)/windows_json.o: src/ipc/windows_json.c src/ipc/windows.h src/util/json.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c src/ipc/windows_json.c -o $@

$(UNITDIR)/outputs_json.o: src/ipc/outputs_json.c src/ipc/outputs.h src/util/json.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c src/ipc/outputs_json.c -o $@

$(UNITDIR)/workspaces_json.o: src/ipc/workspaces_json.c src/ipc/workspaces.h src/util/json.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c src/ipc/workspaces_json.c -o $@

$(UNITDIR)/layers_json.o: src/ipc/layers_json.c src/ipc/layers.h src/util/json.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c src/ipc/layers_json.c -o $@

$(UNITDIR)/util_math_test: $(UNITDIR)/sf_test.o $(UNITDIR)/math_test.o
	$(CC) $(COVFLAGS) -o $@ $^ -lm

$(UNITDIR)/util_color_test: $(UNITDIR)/sf_test.o $(UNITDIR)/color_test.o
	$(CC) $(COVFLAGS) -o $@ $^ -lm

$(UNITDIR)/util_time_test: $(UNITDIR)/sf_test.o $(UNITDIR)/time_test.o
	$(CC) $(COVFLAGS) -o $@ $^

$(UNITDIR)/util_str_test: $(UNITDIR)/sf_test.o $(UNITDIR)/str_test.o $(UNITDIR)/log_stub.o $(UNITDIR)/str.o
	$(CC) $(COVFLAGS) -o $@ $^

$(UNITDIR)/util_json_test: $(UNITDIR)/sf_test.o $(UNITDIR)/json_test.o $(UNITDIR)/json.o
	$(CC) $(COVFLAGS) -o $@ $^

$(UNITDIR)/ssd_layout_test: $(UNITDIR)/sf_test.o $(UNITDIR)/ssd_layout_test.o $(UNITDIR)/ssd_layout.o
	$(CC) $(COVFLAGS) -o $@ $^

$(UNITDIR)/ssd_policy_test: $(UNITDIR)/sf_test.o $(UNITDIR)/ssd_policy_test.o $(UNITDIR)/ssd_policy.o
	$(CC) $(COVFLAGS) -o $@ $^

$(UNITDIR)/config_action_test: $(UNITDIR)/sf_test.o $(UNITDIR)/config_action_test.o $(UNITDIR)/action.o $(UNITDIR)/log_stub.o $(UNITDIR)/str.o
	$(CC) $(COVFLAGS) -o $@ $^

$(UNITDIR)/ipc_windows_json_test: $(UNITDIR)/sf_test.o $(UNITDIR)/ipc_windows_json_test.o $(UNITDIR)/windows_json.o $(UNITDIR)/json.o
	$(CC) $(COVFLAGS) -o $@ $^

$(UNITDIR)/ipc_outputs_json_test: $(UNITDIR)/sf_test.o $(UNITDIR)/ipc_outputs_json_test.o $(UNITDIR)/outputs_json.o $(UNITDIR)/json.o
	$(CC) $(COVFLAGS) -o $@ $^ -lm

$(UNITDIR)/ipc_workspaces_json_test: $(UNITDIR)/sf_test.o $(UNITDIR)/ipc_workspaces_json_test.o $(UNITDIR)/workspaces_json.o $(UNITDIR)/json.o
	$(CC) $(COVFLAGS) -o $@ $^

$(UNITDIR)/ipc_layers_json_test: $(UNITDIR)/sf_test.o $(UNITDIR)/ipc_layers_json_test.o $(UNITDIR)/layers_json.o $(UNITDIR)/json.o
	$(CC) $(COVFLAGS) -o $@ $^


$(UNITDIR)/shell_quote_test.o: tests/util/shell_quote_test.c tests/sf_test.h src/util/shell_quote.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c tests/util/shell_quote_test.c -o $@

$(UNITDIR)/wm_geom_test.o: tests/wm/geom_test.c tests/sf_test.h src/wm/geom.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c tests/wm/geom_test.c -o $@

$(UNITDIR)/wm_layout_test.o: tests/wm/layout_test.c tests/sf_test.h src/wm/workspace.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c tests/wm/layout_test.c -o $@

$(UNITDIR)/snap_test.o: tests/wm/snap_test.c tests/sf_test.h src/wm/snap.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c tests/wm/snap_test.c -o $@

$(UNITDIR)/ipc_parse_test.o: tests/ipc/parse_test.c tests/sf_test.h src/ipc/parse.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c tests/ipc/parse_test.c -o $@

$(UNITDIR)/shell_quote.o: src/util/shell_quote.c src/util/shell_quote.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c src/util/shell_quote.c -o $@

$(UNITDIR)/geom.o: src/wm/geom.c src/wm/geom.h src/wm/snap.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c src/wm/geom.c -o $@

$(UNITDIR)/layout.o: src/wm/layout.c src/wm/workspace.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c src/wm/layout.c -o $@

$(UNITDIR)/parse.o: src/ipc/parse.c src/ipc/parse.h src/util/json.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c src/ipc/parse.c -o $@

$(UNITDIR)/util_shell_quote_test: $(UNITDIR)/sf_test.o $(UNITDIR)/shell_quote_test.o $(UNITDIR)/shell_quote.o
	$(CC) $(COVFLAGS) -o $@ $^

$(UNITDIR)/wm_geom_test: $(UNITDIR)/sf_test.o $(UNITDIR)/wm_geom_test.o $(UNITDIR)/geom.o
	$(CC) $(COVFLAGS) -o $@ $^

$(UNITDIR)/wm_layout_test: $(UNITDIR)/sf_test.o $(UNITDIR)/wm_layout_test.o $(UNITDIR)/layout.o
	$(CC) $(COVFLAGS) -o $@ $^

$(UNITDIR)/wm_snap_test: $(UNITDIR)/sf_test.o $(UNITDIR)/snap_test.o
	$(CC) $(COVFLAGS) -o $@ $^

$(UNITDIR)/ipc_parse_test: $(UNITDIR)/sf_test.o $(UNITDIR)/ipc_parse_test.o $(UNITDIR)/parse.o $(UNITDIR)/json.o
	$(CC) $(COVFLAGS) -o $@ $^


$(UNITDIR)/path_test.o: tests/util/path_test.c tests/sf_test.h src/util/path.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c tests/util/path_test.c -o $@

$(UNITDIR)/config_chord_test.o: tests/config/chord_test.c tests/sf_test.h src/config/chord.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c tests/config/chord_test.c -o $@

$(UNITDIR)/path.o: src/util/path.c src/util/path.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c src/util/path.c -o $@

$(UNITDIR)/chord.o: src/config/chord.c src/config/chord.h
	mkdir -p "$(UNITDIR)"
	$(CC) $(CFLAGS_TEST) $(COVFLAGS) -I tests/stubs/include -I tests -I src -c src/config/chord.c -o $@

$(UNITDIR)/util_path_test: $(UNITDIR)/sf_test.o $(UNITDIR)/path_test.o $(UNITDIR)/path.o
	$(CC) $(COVFLAGS) -o $@ $^

$(UNITDIR)/config_chord_test: $(UNITDIR)/sf_test.o $(UNITDIR)/config_chord_test.o $(UNITDIR)/chord.o
	$(CC) $(COVFLAGS) -o $@ $^

clean:
	rm -f "$(TARBALL)"
	rm -rf "$(APORT)/src" "$(APORT)/pkg" build/unit build/unit-cover build/coverage
