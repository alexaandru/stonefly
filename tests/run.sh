#!/bin/sh
# Go-style unit runner. OPTS=verbose,cover (comma-separated).
# Makefile defaults OPTS to cover (OPTS ?= cover). Explicit OPTS= or
# OPTS=verbose disables cover unless cover is listed again.
# Layout uses fixed-width columns (spaces).
# File rows are grouped by src/ folder; each folder rollup follows its files.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"

OPTS=${OPTS:-}
CC=${CC:-cc}
CFLAGS_TEST=${CFLAGS_TEST:--std=c23 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L}

verbose=0
cover=0
old_ifs=$IFS
IFS=,
for opt in $OPTS; do
	case $opt in
	verbose) verbose=1 ;;
	cover) cover=1 ;;
	"") ;;
	*)
		printf 'tests/run.sh: unknown OPTS entry: %s\n' "$opt" >&2
		exit 2
		;;
	esac
done
IFS=$old_ifs

if [ "$cover" -eq 1 ]; then
	UNITDIR=build/unit-cover
else
	UNITDIR=build/unit
fi

if [ "$verbose" -eq 1 ]; then
	export SF_TEST_VERBOSE=1
else
	unset SF_TEST_VERBOSE || true
fi

if [ -t 2 ] || [ "${SF_TEST_COLOR:-}" = "1" ]; then
	RED=$(printf '\033[31m')
	RESET=$(printf '\033[0m')
else
	RED=
	RESET=
fi

# id | binary | gcov obj | covered src path under src/
SUITES='util/math.h|util_math_test|math_test.o|util/math.h
util/color.h|util_color_test|color_test.o|util/color.h
util/time.h|util_time_test|time_test.o|util/time.h
util/str.c|util_str_test|str.o|util/str.c
util/json.c|util_json_test|json.o|util/json.c
util/shell_quote.c|util_shell_quote_test|shell_quote.o|util/shell_quote.c
util/path.c|util_path_test|path.o|util/path.c
ssd/ssd_layout.c|ssd_layout_test|ssd_layout.o|ssd/ssd_layout.c
ssd/ssd_policy.c|ssd_policy_test|ssd_policy.o|ssd/ssd_policy.c
config/action.c|config_action_test|action.o|config/action.c
config/chord.c|config_chord_test|chord.o|config/chord.c
wm/geom.c|wm_geom_test|geom.o|wm/geom.c
wm/layout.c|wm_layout_test|layout.o|wm/layout.c
wm/snap.h|wm_snap_test|snap_test.o|wm/snap.h
ipc/parse.c|ipc_parse_test|parse.o|ipc/parse.c
ipc/windows_json.c|ipc_windows_json_test|windows_json.o|ipc/windows_json.c
ipc/outputs_json.c|ipc_outputs_json_test|outputs_json.o|ipc/outputs_json.c
ipc/workspaces_json.c|ipc_workspaces_json_test|workspaces_json.o|ipc/workspaces_json.c
ipc/layers_json.c|ipc_layers_json_test|layers_json.o|ipc/layers_json.c'

# Fixed columns: STATUS(4)  PATH(20)  TIME(8)  EXTRA
# ANSI on FAIL does not affect PATH column (color only wraps the 4-char label).
print_row() {
	# Use _pr_* names: POSIX sh functions share the caller's globals.
	_pr_to_err=$1
	_pr_status=$2
	_pr_path=$3
	_pr_elapsed=$4
	_pr_extra=${5:-}
	_pr_line=$(
		COL_RED="$RED" COL_RESET="$RESET" \
			STATUS="$_pr_status" ROW_PATH="$_pr_path" ELAPSED="$_pr_elapsed" \
			EXTRA="$_pr_extra" \
			python3 <<'PY'
import os

red = os.environ.get("COL_RED", "")
reset = os.environ.get("COL_RESET", "")
status = os.environ["STATUS"]
path = os.environ["ROW_PATH"]
elapsed = os.environ["ELAPSED"]
extra = os.environ.get("EXTRA", "")

if status == "FAIL" and red:
    label = f"{red}FAIL{reset}"
else:
    label = f"{status:<4}"

row = f"{label}  {path:<24}{elapsed:>8}"
if extra:
    row += f"  {extra}"
print(row)
PY
	)
	if [ "$_pr_to_err" -eq 1 ]; then
		printf '%s\n' "$_pr_line" >&2
	else
		printf '%s\n' "$_pr_line"
	fi
}

now() {
	python3 -c 'import time; print(time.perf_counter())'
}

# Print "pct" for covered file from gcov -n; empty on miss.
gcov_pct() {
	obj=$1
	want=$2
	python3 - "$UNITDIR" "$obj" "$want" <<'PY'
import re, subprocess, sys

unit, obj, want = sys.argv[1], sys.argv[2], sys.argv[3]
r = subprocess.run(
    ["gcov", "-n", obj],
    cwd=unit,
    capture_output=True,
    text=True,
)
file_re = re.compile(r"^File '([^']+)'")
lines_re = re.compile(r"Lines executed:([0-9.]+)% of ([0-9]+)")
cur = None
for line in r.stdout.splitlines():
    m = file_re.match(line)
    if m:
        cur = m.group(1)
        continue
    m = lines_re.search(line)
    if not m or cur is None:
        continue
    path = cur[4:] if cur.startswith("src/") else cur
    if path == want or cur.endswith(want):
        print(f"{float(m.group(1)):.1f}")
        raise SystemExit(0)
raise SystemExit(1)
PY
}

# Folder rollup after that folder's file rows (cover mode only).
# Uses covmap + folder_fail (one folder name per line on any suite fail).
print_folder_row() {
	# _pf_* names: POSIX sh functions share the caller's globals.
	_pf_folder=$1
	if [ "$cover" -ne 1 ]; then
		return 0
	fi
	_pf_failed=0
	if grep -qx "$_pf_folder" "$folder_fail" 2>/dev/null; then
		_pf_failed=1
	fi
	_pf_row=$(
		COVMAP="$covmap" FOLDER="$_pf_folder" FAILED="$_pf_failed" python3 <<'PY'
import os
from pathlib import Path

folder = os.environ["FOLDER"]
failed = os.environ["FAILED"] == "1"
measured = {}
for line in open(os.environ["COVMAP"]):
    parts = line.split()
    if len(parts) >= 2:
        measured[parts[0]] = float(parts[1])

vals = []
for p in Path("src").rglob("*"):
    if not p.is_file() or p.suffix not in {".c", ".h"}:
        continue
    rel = str(p.relative_to("src"))
    if rel.split("/", 1)[0] + "/" != folder:
        continue
    vals.append(measured.get(rel, 0.0))

avg = sum(vals) / len(vals) if vals else 0.0
hit = sum(1 for v in vals if v > 0.0)
print(f"{avg:.1f} {hit} {len(vals)} {1 if failed else 0}")
PY
	)
	_pf_avg=$(printf '%s' "$_pf_row" | cut -d' ' -f1)
	_pf_hit=$(printf '%s' "$_pf_row" | cut -d' ' -f2)
	_pf_n=$(printf '%s' "$_pf_row" | cut -d' ' -f3)
	_pf_failed=$(printf '%s' "$_pf_row" | cut -d' ' -f4)
	_pf_note=$(printf 'coverage: %s%% of lines / %s out of %s files' \
		"$_pf_avg" "$_pf_hit" "$_pf_n")
	if [ "$_pf_failed" -eq 1 ]; then
		print_row 1 "FAIL" "$_pf_folder" "" "$_pf_note"
	else
		print_row 0 "ok" "$_pf_folder" "" "$_pf_note"
	fi
}

make_flags="--no-print-directory"
if [ "$verbose" -eq 0 ]; then
	make_flags="$make_flags -s"
fi
# shellcheck disable=SC2086
MAKEFLAGS= make $make_flags UNITDIR="$UNITDIR" CFLAGS_TEST="$CFLAGS_TEST" CC="$CC" \
	"$UNITDIR/util_math_test" \
	"$UNITDIR/util_color_test" \
	"$UNITDIR/util_time_test" \
	"$UNITDIR/util_str_test" \
	"$UNITDIR/util_json_test" \
	"$UNITDIR/util_shell_quote_test" \
	"$UNITDIR/util_path_test" \
	"$UNITDIR/ssd_layout_test" \
	"$UNITDIR/ssd_policy_test" \
	"$UNITDIR/config_action_test" \
	"$UNITDIR/config_chord_test" \
	"$UNITDIR/wm_geom_test" \
	"$UNITDIR/wm_layout_test" \
	"$UNITDIR/wm_snap_test" \
	"$UNITDIR/ipc_parse_test" \
	"$UNITDIR/ipc_windows_json_test" \
	"$UNITDIR/ipc_outputs_json_test" \
	"$UNITDIR/ipc_workspaces_json_test" \
	"$UNITDIR/ipc_layers_json_test"

if [ "$cover" -eq 1 ]; then
	rm -f "$UNITDIR"/*.gcda
fi

covmap=$(mktemp)
err=$(mktemp)
folder_fail=$(mktemp)
suite_list=$(mktemp)
trap 'rm -f "$covmap" "$err" "$folder_fail" "$suite_list"' EXIT
: >"$covmap"
: >"$folder_fail"
printf '%s\n' "$SUITES" >"$suite_list"

prev_folder=
ok_files=0
fail_files=0
ok_funcs=0
fail_funcs=0

while IFS= read -r line; do
	[ -n "$line" ] || continue
	id=$(printf '%s' "$line" | cut -d'|' -f1)
	bin=$(printf '%s' "$line" | cut -d'|' -f2)
	obj=$(printf '%s' "$line" | cut -d'|' -f3)
	src=$(printf '%s' "$line" | cut -d'|' -f4)
	folder=$(printf '%s' "$id" | cut -d'/' -f1)/
	binpath=$UNITDIR/$bin

	if [ -n "$prev_folder" ] && [ "$folder" != "$prev_folder" ]; then
		print_folder_row "$prev_folder"
	fi
	prev_folder=$folder

	t0=$(now)
	set +e
	result=$("$binpath" 2>"$err")
	ec=$?
	set -e
	t1=$(now)
	elapsed=$(python3 -c "print(f'{float('$t1')-float('$t0'):.3f}s')")

	pass=0
	fail=0
	if printf '%s' "$result" | grep -q '^sf_test_result '; then
		pass=$(printf '%s' "$result" | sed -n 's/^sf_test_result cases=[0-9]* pass=\([0-9]*\).*/\1/p')
		fail=$(printf '%s' "$result" | sed -n 's/^sf_test_result cases=[0-9]* pass=[0-9]* fail=\([0-9]*\).*/\1/p')
	fi

	if [ "$ec" -eq 0 ]; then
		if [ "$verbose" -eq 1 ]; then
			cat "$err" >&2
		fi
		cov_note=
		if [ "$cover" -eq 1 ]; then
			pct=$(gcov_pct "$obj" "$src" || true)
			if [ -z "${pct:-}" ]; then
				pct=0.0
			fi
			printf '%s %s\n' "$src" "$pct" >>"$covmap"
			cov_note=$(printf 'coverage: %s%% of lines' "$pct")
		fi
		print_row 0 "ok" "$id" "$elapsed" "$cov_note"
		ok_files=$((ok_files + 1))
		ok_funcs=$((ok_funcs + pass))
		fail_funcs=$((fail_funcs + fail))
	else
		cov_note=
		if [ "$cover" -eq 1 ]; then
			printf '%s 0.0\n' "$src" >>"$covmap"
			cov_note=$(printf 'coverage: 0.0%% of lines')
		fi
		print_row 1 "FAIL" "$id" "$elapsed" "$cov_note"
		cat "$err" >&2
		fail_files=$((fail_files + 1))
		ok_funcs=$((ok_funcs + pass))
		fail_funcs=$((fail_funcs + fail))
		printf '%s\n' "$folder" >>"$folder_fail"
	fi
done <"$suite_list"

if [ -n "$prev_folder" ]; then
	print_folder_row "$prev_folder"
fi

overall=
covered_n=
src_n=
if [ "$cover" -eq 1 ]; then
	overall_line=$(
		COVMAP="$covmap" python3 <<'PY'
from pathlib import Path
import os

measured = {}
for line in open(os.environ["COVMAP"]):
    parts = line.split()
    if len(parts) >= 2:
        measured[parts[0]] = float(parts[1])

pcts = []
for p in Path("src").rglob("*"):
    if p.is_file() and p.suffix in {".c", ".h"}:
        rel = str(p.relative_to("src"))
        pcts.append(measured.get(rel, 0.0))
overall = sum(pcts) / len(pcts) if pcts else 0.0
covered_n = sum(1 for v in pcts if v > 0.0)
print(f"{overall:.1f} {covered_n} {len(pcts)}")
PY
	)
	overall=$(printf '%s' "$overall_line" | cut -d' ' -f1)
	covered_n=$(printf '%s' "$overall_line" | cut -d' ' -f2)
	src_n=$(printf '%s' "$overall_line" | cut -d' ' -f3)
fi

printf '------------------------------------------------------------------------------------\n'
ok_extra=
if [ "$cover" -eq 1 ]; then
	ok_extra=$(printf 'coverage: %s%% of lines / %s out of %s files' \
		"$overall" "$covered_n" "$src_n")
fi
print_row 0 "OK" "$ok_files files / $ok_funcs functions" "" "$ok_extra"
if [ "$fail_files" -gt 0 ]; then
	print_row 1 "FAIL" "$fail_files files / $fail_funcs functions" ""
fi

if [ "$fail_files" -gt 0 ]; then
	exit 1
fi
exit 0
