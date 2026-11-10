#!/bin/sh
# Go-style unit runner. OPTS=verbose,cover (comma-separated).
# Makefile defaults OPTS to cover (OPTS ?= cover). Explicit OPTS= or
# OPTS=verbose disables cover unless cover is listed again.
# Layout uses fixed-width columns (spaces).
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
util/json.c|util_json_test|json.o|util/json.c'

# Fixed columns: STATUS(4)  PATH(20)  TIME(8)  EXTRA
# ANSI on FAIL does not affect PATH column (color only wraps the 4-char label).
print_row() {
	to_err=$1
	status=$2
	path=$3
	elapsed=$4
	extra=${5:-}
	line=$(
		COL_RED="$RED" COL_RESET="$RESET" \
			STATUS="$status" ROW_PATH="$path" ELAPSED="$elapsed" EXTRA="$extra" \
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

row = f"{label}  {path:<20}{elapsed:>8}"
if extra:
    row += f"  {extra}"
print(row)
PY
	)
	if [ "$to_err" -eq 1 ]; then
		printf '%s\n' "$line" >&2
	else
		printf '%s\n' "$line"
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
	"$UNITDIR/util_json_test"

if [ "$cover" -eq 1 ]; then
	rm -f "$UNITDIR"/*.gcda
fi

stats=$(mktemp)
covmap=$(mktemp)
err=$(mktemp)
trap 'rm -f "$stats" "$covmap" "$err"' EXIT
: >"$stats"
: >"$covmap"

printf '%s\n' "$SUITES" | while IFS= read -r line; do
	[ -n "$line" ] || continue
	id=$(printf '%s' "$line" | cut -d'|' -f1)
	bin=$(printf '%s' "$line" | cut -d'|' -f2)
	obj=$(printf '%s' "$line" | cut -d'|' -f3)
	src=$(printf '%s' "$line" | cut -d'|' -f4)
	path=$UNITDIR/$bin

	t0=$(now)
	set +e
	result=$("$path" 2>"$err")
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
		printf 'ok %s %s %s\n' "$src" "$pass" "$fail" >>"$stats"
	else
		cov_note=
		if [ "$cover" -eq 1 ]; then
			printf '%s 0.0\n' "$src" >>"$covmap"
			cov_note=$(printf 'coverage: 0.0%% of lines')
		fi
		print_row 1 "FAIL" "$id" "$elapsed" "$cov_note"
		cat "$err" >&2
		printf 'fail %s %s %s\n' "$src" "$pass" "$fail" >>"$stats"
	fi
done

footer=$(python3 - "$stats" "$covmap" "$cover" <<'PY'
import sys
from pathlib import Path

stats_path, covmap_path, cover = sys.argv[1], sys.argv[2], sys.argv[3] == "1"
ok_files = fail_files = 0
ok_funcs = fail_funcs = 0
util_fail = 0
for line in open(stats_path):
    kind, path, p, f = line.split()
    p, f = int(p), int(f)
    if kind == "ok":
        ok_files += 1
        ok_funcs += p
        fail_funcs += f
    else:
        fail_files += 1
        ok_funcs += p
        fail_funcs += f
        if path.startswith("util/"):
            util_fail = 1

print(f"{ok_files} {fail_files} {ok_funcs} {fail_funcs}")
if not cover:
    raise SystemExit(0)

measured = {}
for line in open(covmap_path):
    path, pct = line.split()
    measured[path] = float(pct)

root = Path(".")
src_files = sorted(
    p for p in root.joinpath("src").rglob("*")
    if p.is_file() and p.suffix in {".c", ".h"}
)
pcts = []
util_pcts = []
for p in src_files:
    rel = str(p.relative_to("src"))
    pct = measured.get(rel, 0.0)
    pcts.append(pct)
    if rel.startswith("util/"):
        util_pcts.append(pct)
util_avg = sum(util_pcts) / len(util_pcts) if util_pcts else 0.0
util_hit = sum(1 for p in util_pcts if p > 0.0)
print(f"util {util_fail} {util_avg:.1f} {util_hit} {len(util_pcts)}")
overall = sum(pcts) / len(pcts) if pcts else 0.0
covered_n = sum(1 for p in pcts if p > 0.0)
print(f"overall {overall:.1f} {covered_n} {len(pcts)}")
PY
)

ok_files=$(printf '%s' "$footer" | sed -n '1p' | cut -d' ' -f1)
fail_files=$(printf '%s' "$footer" | sed -n '1p' | cut -d' ' -f2)
ok_funcs=$(printf '%s' "$footer" | sed -n '1p' | cut -d' ' -f3)
fail_funcs=$(printf '%s' "$footer" | sed -n '1p' | cut -d' ' -f4)

if [ "$cover" -eq 1 ]; then
	util_fail=$(printf '%s' "$footer" | sed -n '2p' | cut -d' ' -f2)
	util_avg=$(printf '%s' "$footer" | sed -n '2p' | cut -d' ' -f3)
	util_hit=$(printf '%s' "$footer" | sed -n '2p' | cut -d' ' -f4)
	util_n=$(printf '%s' "$footer" | sed -n '2p' | cut -d' ' -f5)
	overall=$(printf '%s' "$footer" | sed -n '3p' | cut -d' ' -f2)
	covered_n=$(printf '%s' "$footer" | sed -n '3p' | cut -d' ' -f3)
	src_n=$(printf '%s' "$footer" | sed -n '3p' | cut -d' ' -f4)
	cov_note=$(printf 'coverage: %s%% of lines / %s out of %s files' \
		"$util_avg" "$util_hit" "$util_n")
	if [ "$util_fail" -eq 1 ]; then
		print_row 1 "FAIL" "util/" "" "$cov_note"
	else
		print_row 0 "ok" "util/" "" "$cov_note"
	fi
fi

printf '--------------------------------------------------------------------------------\n'
print_row 0 "OK" "$ok_files files / $ok_funcs functions" ""
print_row 1 "FAIL" "$fail_files files / $fail_funcs functions" ""

if [ "$cover" -eq 1 ]; then
	printf 'coverage: %s%% of lines / %s out of %s files\n' \
		"$overall" "$covered_n" "$src_n"
fi

if [ "$fail_files" -gt 0 ]; then
	exit 1
fi
exit 0
