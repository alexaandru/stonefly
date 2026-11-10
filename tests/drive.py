#!/usr/bin/env python3
"""Discover, build, run, and cover stonefly unit suites.

Suites are tests/**/*_test.c with a pragma near the top:

	/* sf-suite cover=util/str.c src=util/str.c stub=log libs=m */

	cover=  path under src/ shown in the report (required)
	src=    comma-separated extra .c files under src/ to link (optional)
	stub=   log  → also compile tests/stubs/log_stub.c (optional)
	libs=   m    → -lm (optional)

Host builds put tests/stubs/include ahead of system headers. Adding a suite
is: drop *_test.c with an sf-suite line — no Makefile / meson edits.
"""

from __future__ import annotations

import os
import re
import shlex
import subprocess
import sys
import tempfile
import time
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / "tests"
SRC = ROOT / "src"
STUB_INC = TESTS / "stubs" / "include"

SUITE_RE = re.compile(
	r"/\*\s*sf-suite\s+(.+?)\s*\*/",
	re.DOTALL,
)


@dataclass
class Suite:
	test_c: Path
	cover: str
	srcs: list[str] = field(default_factory=list)
	stub_log: bool = False
	libs: list[str] = field(default_factory=list)

	@property
	def name(self) -> str:
		# tests/util/str_test.c → util_str_test
		rel = self.test_c.relative_to(TESTS)
		return "_".join(rel.with_suffix("").parts)

	@property
	def folder(self) -> str:
		return self.cover.split("/", 1)[0] + "/"


def parse_suite(path: Path) -> Suite:
	head = path.read_text(encoding="utf-8", errors="replace")[:4096]
	m = SUITE_RE.search(head)
	if not m:
		raise SystemExit(f"{path}: missing /* sf-suite … */ pragma")
	cover = None
	srcs: list[str] = []
	stub_log = False
	libs: list[str] = []
	for tok in m.group(1).split():
		if "=" not in tok:
			raise SystemExit(f"{path}: bad sf-suite token {tok!r}")
		k, v = tok.split("=", 1)
		if k == "cover":
			cover = v
		elif k == "src":
			srcs = [p for p in v.split(",") if p]
		elif k == "stub":
			stub_log = "log" in v.split(",")
		elif k == "libs":
			libs = [p for p in v.split(",") if p]
		else:
			raise SystemExit(f"{path}: unknown sf-suite key {k!r}")
	if not cover:
		raise SystemExit(f"{path}: sf-suite needs cover=")
	return Suite(test_c=path, cover=cover, srcs=srcs, stub_log=stub_log, libs=libs)


def discover() -> list[Suite]:
	# *_test.c only — exclude the harness itself (sf_test.c).
	paths = sorted(
		p for p in TESTS.rglob("*_test.c") if p.name != "sf_test.c"
	)
	return [parse_suite(p) for p in paths]


def parse_opts(opts: str) -> tuple[bool, bool]:
	verbose = False
	cover = False
	for opt in opts.split(","):
		opt = opt.strip()
		if opt == "verbose":
			verbose = True
		elif opt == "cover":
			cover = True
		elif opt == "":
			continue
		else:
			raise SystemExit(f"tests/drive.py: unknown OPTS entry: {opt}")
	return verbose, cover


def color_enabled() -> bool:
	return sys.stderr.isatty() or os.environ.get("SF_TEST_COLOR") == "1"


def print_row(to_err: bool, status: str, path: str, elapsed: str = "", extra: str = "") -> None:
	red = "\033[31m" if color_enabled() else ""
	reset = "\033[0m" if color_enabled() else ""
	if status == "FAIL" and red:
		label = f"{red}FAIL{reset}"
	else:
		label = f"{status:<4}"
	row = f"{label}  {path:<24}{elapsed:>8}"
	if extra:
		row += f"  {extra}"
	print(row, file=sys.stderr if to_err else sys.stdout)


def newer(target: Path, inputs: list[Path]) -> bool:
	if not target.is_file():
		return True
	mt = target.stat().st_mtime
	return any(inp.stat().st_mtime > mt for inp in inputs)


def run_cmd(cmd: list[str], verbose: bool) -> None:
	if verbose:
		print("+", " ".join(shlex.quote(c) for c in cmd), file=sys.stderr)
	subprocess.check_call(cmd)


def compile_obj(
	cc: str,
	cflags: list[str],
	covflags: list[str],
	src: Path,
	obj: Path,
	verbose: bool,
	extra_inputs: list[Path] | None = None,
) -> None:
	inputs = [src] + (extra_inputs or [])
	if not newer(obj, inputs):
		return
	obj.parent.mkdir(parents=True, exist_ok=True)
	cmd = [cc, *cflags, *covflags, "-c", str(src), "-o", str(obj)]
	run_cmd(cmd, verbose)


def gcov_pct(unitdir: Path, obj: Path, want: str) -> float | None:
	r = subprocess.run(
		["gcov", "-n", str(obj.name)],
		cwd=unitdir,
		capture_output=True,
		text=True,
		check=False,
	)
	cur = None
	file_re = re.compile(r"^File '([^']+)'")
	lines_re = re.compile(r"Lines executed:([0-9.]+)% of ([0-9]+)")
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
			return float(m.group(1))
	return None


def folder_stats(folder: str, measured: dict[str, float]) -> tuple[float, int, int]:
	vals: list[float] = []
	for p in SRC.rglob("*"):
		if not p.is_file() or p.suffix not in {".c", ".h"}:
			continue
		rel = str(p.relative_to(SRC))
		if rel.split("/", 1)[0] + "/" != folder:
			continue
		vals.append(measured.get(rel, 0.0))
	avg = sum(vals) / len(vals) if vals else 0.0
	hit = sum(1 for v in vals if v > 0.0)
	return avg, hit, len(vals)


def overall_stats(measured: dict[str, float]) -> tuple[float, int, int]:
	pcts: list[float] = []
	for p in SRC.rglob("*"):
		if p.is_file() and p.suffix in {".c", ".h"}:
			rel = str(p.relative_to(SRC))
			pcts.append(measured.get(rel, 0.0))
	avg = sum(pcts) / len(pcts) if pcts else 0.0
	hit = sum(1 for v in pcts if v > 0.0)
	return avg, hit, len(pcts)


def build_suite(
	suite: Suite,
	cc: str,
	cflags: list[str],
	covflags: list[str],
	unitdir: Path,
	sf_test_o: Path,
	verbose: bool,
) -> Path:
	objs = [sf_test_o]
	if suite.stub_log:
		stub_c = TESTS / "stubs" / "log_stub.c"
		stub_o = unitdir / "log_stub.o"
		compile_obj(cc, cflags, covflags, stub_c, stub_o, verbose, [ROOT / "src" / "log" / "log.h"])
		objs.append(stub_o)

	for rel in suite.srcs:
		src = SRC / rel
		if not src.is_file():
			raise SystemExit(f"{suite.test_c}: src={rel} missing")
		obj = unitdir / (rel.replace("/", "_") + ".o")
		compile_obj(cc, cflags, covflags, src, obj, verbose)
		objs.append(obj)

	test_o = unitdir / (suite.name + ".o")
	compile_obj(cc, cflags, covflags, suite.test_c, test_o, verbose, [TESTS / "sf_test.h"])
	objs.append(test_o)

	binpath = unitdir / suite.name
	libflags = [f"-l{lib}" for lib in suite.libs]
	if newer(binpath, objs):
		cmd = [cc, *covflags, "-o", str(binpath), *[str(o) for o in objs], *libflags]
		run_cmd(cmd, verbose)
	return binpath


def main() -> int:
	os.chdir(ROOT)
	opts = os.environ.get("OPTS", "")
	verbose, cover = parse_opts(opts)
	cc = os.environ.get("CC", "cc")
	cflags_env = os.environ.get(
		"CFLAGS_TEST",
		"-std=c23 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L",
	)
	cflags = shlex.split(cflags_env) + [
		"-I",
		str(STUB_INC),
		"-I",
		str(TESTS),
		"-I",
		str(SRC),
	]
	unitdir = ROOT / ("build/unit-cover" if cover else "build/unit")
	covflags = ["--coverage"] if cover else []
	unitdir.mkdir(parents=True, exist_ok=True)

	if verbose:
		os.environ["SF_TEST_VERBOSE"] = "1"
	else:
		os.environ.pop("SF_TEST_VERBOSE", None)

	suites = discover()
	if not suites:
		raise SystemExit("tests/drive.py: no *_test.c suites found")

	# Shared harness object.
	sf_test_o = unitdir / "sf_test.o"
	compile_obj(cc, cflags, covflags, TESTS / "sf_test.c", sf_test_o, verbose, [TESTS / "sf_test.h"])

	# Build all binaries first (fail fast on compile).
	bins: dict[str, Path] = {}
	cov_objs: dict[str, Path] = {}
	for suite in suites:
		bins[suite.cover] = build_suite(suite, cc, cflags, covflags, unitdir, sf_test_o, verbose)
		if suite.srcs:
			# Primary covered TU is first src when it matches cover stem; else first src.
			primary = None
			for rel in suite.srcs:
				if rel == suite.cover or Path(rel).name == Path(suite.cover).name:
					primary = rel
					break
			if primary is None:
				primary = suite.srcs[0]
			cov_objs[suite.cover] = unitdir / (primary.replace("/", "_") + ".o")
		else:
			# Header-only: coverage attributed to the test object (like before).
			cov_objs[suite.cover] = unitdir / (suite.name + ".o")

	if cover:
		for gcda in unitdir.glob("*.gcda"):
			gcda.unlink()

	measured: dict[str, float] = {}
	folder_fail: set[str] = set()
	ok_files = fail_files = 0
	ok_funcs = fail_funcs = 0
	prev_folder: str | None = None

	def emit_folder(folder: str) -> None:
		if not cover:
			return
		avg, hit, n = folder_stats(folder, measured)
		note = f"coverage: {avg:.1f}% of lines / {hit} out of {n} files"
		if folder in folder_fail:
			print_row(True, "FAIL", folder, "", note)
		else:
			print_row(False, "ok", folder, "", note)

	for suite in suites:
		if prev_folder is not None and suite.folder != prev_folder:
			emit_folder(prev_folder)
		prev_folder = suite.folder

		binpath = bins[suite.cover]
		t0 = time.perf_counter()
		proc = subprocess.run(
			[str(binpath)],
			capture_output=True,
			text=True,
			check=False,
		)
		elapsed = f"{time.perf_counter() - t0:.3f}s"

		pass_n = fail_n = 0
		for line in proc.stdout.splitlines():
			if line.startswith("sf_test_result "):
				# sf_test_result cases=N pass=P fail=F
				parts = dict(
					kv.split("=", 1) for kv in line.split()[1:] if "=" in kv
				)
				pass_n = int(parts.get("pass", "0"))
				fail_n = int(parts.get("fail", "0"))

		if proc.returncode == 0:
			if verbose and proc.stderr:
				sys.stderr.write(proc.stderr)
			extra = ""
			if cover:
				pct = gcov_pct(unitdir, cov_objs[suite.cover], suite.cover)
				if pct is None:
					pct = 0.0
				measured[suite.cover] = pct
				extra = f"coverage: {pct:.1f}% of lines"
			print_row(False, "ok", suite.cover, elapsed, extra)
			ok_files += 1
			ok_funcs += pass_n
			fail_funcs += fail_n
		else:
			extra = ""
			if cover:
				measured[suite.cover] = 0.0
				extra = "coverage: 0.0% of lines"
			print_row(True, "FAIL", suite.cover, elapsed, extra)
			if proc.stderr:
				sys.stderr.write(proc.stderr)
			fail_files += 1
			ok_funcs += pass_n
			fail_funcs += fail_n
			folder_fail.add(suite.folder)

	if prev_folder is not None:
		emit_folder(prev_folder)

	print("--------------------------------------------------------------------------------")
	ok_extra = ""
	if cover:
		avg, hit, n = overall_stats(measured)
		ok_extra = f"coverage: {avg:.1f}% of lines / {hit} out of {n} files"
	print_row(False, "OK", f"{ok_files} files / {ok_funcs} functions", "", ok_extra)
	if fail_files > 0:
		print_row(True, "FAIL", f"{fail_files} files / {fail_funcs} functions", "")

	return 1 if fail_files else 0


if __name__ == "__main__":
	try:
		sys.exit(main())
	except subprocess.CalledProcessError as e:
		print(f"tests/drive.py: build failed: {e}", file=sys.stderr)
		sys.exit(2)
