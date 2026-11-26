#!/usr/bin/env luajit
-- Generated from tests/drive.fnl — edit the .fnl, then: make drive-lua
-- FFI: clock_gettime for wall-clock suite timing (os.clock skips children);
-- setenv/unsetenv for SF_TEST_VERBOSE on spawned *_test binaries.
local ffi = require("ffi")
ffi.cdef("typedef long time_t;\ntypedef struct { time_t tv_sec; long tv_nsec; } timespec;\nint clock_gettime(int clk_id, timespec *tp);\nint setenv(const char *name, const char *value, int overwrite);\nint unsetenv(const char *name);")
local CLOCK_MONOTONIC = 1
local function now()
  local ts = ffi.new("timespec")
  if (ffi.C.clock_gettime(CLOCK_MONOTONIC, ts) ~= 0) then
    return os.clock()
  else
    return (tonumber(ts.tv_sec) + (tonumber(ts.tv_nsec) * 1e-09))
  end
end
local function script_dir()
  local raw = debug.getinfo(1, "S").source
  local src
  if (raw:sub(1, 1) == "@") then
    src = raw:sub(2)
  else
    src = raw
  end
  return (src:match("^(.*)/[^/]+$") or ".")
end
local paths
do
  local root
  do
    local dir = script_dir()
    root = (dir:match("^(.*)/[^/]+$") or (dir .. "/.."))
  end
  local tests = (root .. "/tests")
  paths = {root = root, tests = tests, src = (root .. "/src"), ["stub-inc"] = (tests .. "/stubs/include")}
end
local function die(msg)
  io.stderr:write((msg .. "\n"))
  return os.exit(1)
end
local function shell_quote(s)
  return ("'" .. tostring(s):gsub("'", "'\\''") .. "'")
end
local function file_exists_3f(path)
  local case_3_ = io.open(path, "r")
  if (nil ~= case_3_) then
    local f = case_3_
    f:close()
    return true
  else
    local _ = case_3_
    return false
  end
end
local function read_head(path, n)
  local case_5_ = io.open(path, "r")
  if (nil ~= case_5_) then
    local f = case_5_
    local data = (f:read(n) or "")
    f:close()
    return data
  else
    local _ = case_5_
    return die((path .. ": cannot read"))
  end
end
local function read_file(path)
  local case_7_ = io.open(path, "r")
  if (nil ~= case_7_) then
    local f = case_7_
    local data = f:read("*a")
    f:close()
    return data
  else
    local _ = case_7_
    return nil
  end
end
local function split_ws(s)
  local tbl_26_ = {}
  local i_27_ = 0
  for tok in s:gmatch("%S+") do
    local val_28_ = tok
    if (nil ~= val_28_) then
      i_27_ = (i_27_ + 1)
      tbl_26_[i_27_] = val_28_
    else
    end
  end
  return tbl_26_
end
local function split_comma(s)
  local tbl_26_ = {}
  local i_27_ = 0
  for part in (s .. ","):gmatch("([^,]*),") do
    local val_28_
    if (part ~= "") then
      val_28_ = part
    else
      val_28_ = nil
    end
    if (nil ~= val_28_) then
      i_27_ = (i_27_ + 1)
      tbl_26_[i_27_] = val_28_
    else
    end
  end
  return tbl_26_
end
local function basename(path)
  return (path:match("([^/]+)$") or path)
end
local function dirname(path)
  return (path:match("^(.*)/[^/]+$") or ".")
end
local function ensure_dir(path)
  if not os.execute(("mkdir -p " .. shell_quote(path))) then
    return die(("mkdir failed: " .. path))
  else
    return nil
  end
end
local function popen_lines(cmd)
  local case_13_ = io.popen(cmd)
  if (nil ~= case_13_) then
    local p = case_13_
    local lines
    do
      local tbl_26_ = {}
      local i_27_ = 0
      for line in p:lines() do
        local val_28_ = line
        if (nil ~= val_28_) then
          i_27_ = (i_27_ + 1)
          tbl_26_[i_27_] = val_28_
        else
        end
      end
      lines = tbl_26_
    end
    p:close()
    return lines
  else
    local _ = case_13_
    return die(("popen failed: " .. cmd))
  end
end
local function sh_split(s)
  local out = {}
  local i = 1
  do
    local n = #s
    while (i <= n) do
      while ((i <= n) and s:sub(i, i):match("%s")) do
        i = (i + 1)
      end
      if (i <= n) then
        local c = s:sub(i, i)
        if ((c == "\"") or (c == "'")) then
          local j = (i + 1)
          local buf = {}
          while ((j <= n) and (s:sub(j, j) ~= c)) do
            table.insert(buf, s:sub(j, j))
            j = (j + 1)
          end
          table.insert(out, table.concat(buf))
          i = (j + 1)
        else
          local j = i
          while ((j <= n) and not s:sub(j, j):match("%s")) do
            j = (j + 1)
          end
          table.insert(out, s:sub(i, (j - 1)))
          i = j
        end
      else
      end
    end
  end
  return out
end
local function parse_suite(path)
  local head = read_head(path, 4096)
  local body = head:match("/%*%s*sf%-suite%s+(.-)%s*%*/")
  if not body then
    die((path .. ": missing /* sf-suite \226\128\166 */ pragma"))
  else
  end
  local fields = {cover = nil, srcs = {}, libs = {}, ["stub-log"] = false}
  for _, tok in ipairs(split_ws(body)) do
    local k, v = tok:match("^([^=]+)=(.*)$")
    if not k then
      die((path .. ": bad sf-suite token " .. tok))
    else
    end
    if (k == "cover") then
      fields.cover = v
    elseif (k == "src") then
      fields.srcs = split_comma(v)
    elseif (k == "stub") then
      for _0, s in ipairs(split_comma(v)) do
        if (s == "log") then
          fields["stub-log"] = true
        else
        end
      end
    elseif (k == "libs") then
      fields.libs = split_comma(v)
    else
      local _0 = k
      die((path .. ": unknown sf-suite key " .. k))
    end
  end
  if not fields.cover then
    die((path .. ": sf-suite needs cover="))
  else
  end
  local rel = path:sub((#paths.tests + 2))
  local name = rel:gsub("%.c$", ""):gsub("/", "_")
  local folder = ((fields.cover:match("^([^/]+)/") or fields.cover) .. "/")
  return {test_c = path, cover = fields.cover, srcs = fields.srcs, stub_log = fields["stub-log"], libs = fields.libs, name = name, folder = folder}
end
local function discover()
  local found = popen_lines(("find " .. shell_quote(paths.tests) .. " -type f -name '*_test.c' ! -name sf_test.c | sort"))
  local tbl_26_ = {}
  local i_27_ = 0
  for _, p in ipairs(found) do
    local val_28_ = parse_suite(p)
    if (nil ~= val_28_) then
      i_27_ = (i_27_ + 1)
      tbl_26_[i_27_] = val_28_
    else
    end
  end
  return tbl_26_
end
local function parse_opts(opts)
  local flags = {cover = false, verbose = false}
  for opt in (opts .. ","):gmatch("([^,]*),") do
    local case_24_ = (opt:match("^%s*(.-)%s*$") or "")
    if (case_24_ == "verbose") then
      flags.verbose = true
    elseif (case_24_ == "cover") then
      flags.cover = true
    elseif (case_24_ == "") then
    elseif (nil ~= case_24_) then
      local bad = case_24_
      die(("tests/drive.lua: unknown OPTS entry: " .. bad))
    else
    end
  end
  return flags
end
local function color_enabled_3f()
  return ((os.getenv("SF_TEST_COLOR") == "1") or (os.execute("test -t 2") == true))
end
local function print_row(to_err, status, path, elapsed, extra)
  local elapsed0 = (elapsed or "")
  local extra0 = (extra or "")
  local color_3f = color_enabled_3f()
  local red
  if color_3f then
    red = "\27[31m"
  else
    red = ""
  end
  local reset
  if color_3f then
    reset = "\27[0m"
  else
    reset = ""
  end
  local label
  if ((status == "FAIL") and color_3f) then
    label = (red .. "FAIL" .. reset)
  else
    label = string.format("%-4s", status)
  end
  local row = string.format("%s  %-24s%8s", label, path, elapsed0)
  local row0
  if (extra0 ~= "") then
    row0 = (row .. "  " .. extra0)
  else
    row0 = row
  end
  local out
  if to_err then
    out = io.stderr
  else
    out = io.stdout
  end
  return out:write((row0 .. "\n"))
end
local function newer_3f(target, inputs)
  local or_31_ = not file_exists_3f(target)
  if not or_31_ then
    local need = false
    for _, inp in ipairs(inputs) do
      if need then break end
      need = os.execute(("test " .. shell_quote(inp) .. " -nt " .. shell_quote(target)))
    end
    or_31_ = need
  end
  return or_31_
end
local function run_cmd(argv, verbose_3f)
  local cmdline
  local _32_
  do
    local tbl_26_ = {}
    local i_27_ = 0
    for _, a in ipairs(argv) do
      local val_28_ = shell_quote(a)
      if (nil ~= val_28_) then
        i_27_ = (i_27_ + 1)
        tbl_26_[i_27_] = val_28_
      else
      end
    end
    _32_ = tbl_26_
  end
  cmdline = table.concat(_32_, " ")
  if verbose_3f then
    io.stderr:write(("+ " .. table.concat(argv, " ") .. "\n"))
  else
  end
  local ok, what, code = os.execute(cmdline)
  if not ok then
    return error(string.format("command failed (%s %s): %s", tostring(what), tostring(code), cmdline), 0)
  else
    return nil
  end
end
local function append_21(dst, src)
  for _, x in ipairs(src) do
    table.insert(dst, x)
  end
  return dst
end
local function compile_obj(cc, cflags, covflags, src, obj, verbose_3f, extra_inputs)
  local inputs = append_21({src}, (extra_inputs or {}))
  if newer_3f(obj, inputs) then
    ensure_dir(dirname(obj))
    local cmd = {cc}
    append_21(cmd, cflags)
    append_21(cmd, covflags)
    table.insert(cmd, "-c")
    table.insert(cmd, src)
    table.insert(cmd, "-o")
    table.insert(cmd, obj)
    return run_cmd(cmd, verbose_3f)
  else
    return nil
  end
end
local function gcov_pct(unitdir, obj, want)
  local case_37_ = io.popen(("cd " .. shell_quote(unitdir) .. " && gcov -n " .. shell_quote(basename(obj)) .. " 2>/dev/null"))
  if (nil ~= case_37_) then
    local p = case_37_
    local st = {cur = nil, pct = nil}
    for line in p:lines() do
      local case_38_ = line:match("^File '([^']+)'")
      if (nil ~= case_38_) then
        local f = case_38_
        st.cur = f
      else
        local _ = case_38_
        local case_39_ = line:match("Lines executed:([0-9.]+)%% of [0-9]+")
        if (nil ~= case_39_) then
          local n = case_39_
          if st.cur then
            local path
            if (st.cur:sub(1, 4) == "src/") then
              path = st.cur:sub(5)
            else
              path = st.cur
            end
            if ((path == want) or (st.cur:sub(( - #want)) == want)) then
              st.pct = tonumber(n)
            else
            end
          else
          end
        else
        end
      end
    end
    p:close()
    return st.pct
  else
    local _ = case_37_
    return nil
  end
end
local function list_src_tree()
  return popen_lines(("find " .. shell_quote(paths.src) .. " -type f \\( -name '*.c' -o -name '*.h' \\) | sort"))
end
local function avg_hit(vals)
  local st = {sum = 0.0, hit = 0}
  for _, v in ipairs(vals) do
    st.sum = (st.sum + v)
    if (v > 0.0) then
      st.hit = (st.hit + 1)
    else
    end
  end
  local n = #vals
  local _47_
  if (n > 0) then
    _47_ = (st.sum / n)
  else
    _47_ = 0.0
  end
  return _47_, st.hit, n
end
local function folder_stats(folder, measured)
  local function _49_()
    local tbl_26_ = {}
    local i_27_ = 0
    for _, p in ipairs(list_src_tree()) do
      local val_28_
      do
        local rel = p:sub((#paths.src + 2))
        if (((rel:match("^([^/]+)/") or rel) .. "/") == folder) then
          val_28_ = (measured[rel] or 0.0)
        else
          val_28_ = nil
        end
      end
      if (nil ~= val_28_) then
        i_27_ = (i_27_ + 1)
        tbl_26_[i_27_] = val_28_
      else
      end
    end
    return tbl_26_
  end
  return avg_hit(_49_())
end
local function overall_stats(measured)
  local function _52_()
    local tbl_26_ = {}
    local i_27_ = 0
    for _, p in ipairs(list_src_tree()) do
      local val_28_ = (measured[p:sub((#paths.src + 2))] or 0.0)
      if (nil ~= val_28_) then
        i_27_ = (i_27_ + 1)
        tbl_26_[i_27_] = val_28_
      else
      end
    end
    return tbl_26_
  end
  return avg_hit(_52_())
end
local function pkg_config(flag, pkg)
  local case_54_ = io.popen(("pkg-config " .. flag .. " " .. pkg .. " 2>/dev/null"))
  if (nil ~= case_54_) then
    local p = case_54_
    local out = (p:read("*a") or "")
    local ok = select(1, p:close())
    return ok, out
  else
    local _ = case_54_
    return false, ""
  end
end
local function suite_pkg_flags(suite)
  local flags = {cflags = {}, link = {}}
  for _, lib in ipairs(suite.libs) do
    if (lib == "pixman-1") then
      local cok, cflags = pkg_config("--cflags", "pixman-1")
      local lok, libs = pkg_config("--libs", "pixman-1")
      if (not cok or not lok) then
        die((suite.test_c .. ": pkg-config pixman-1 failed"))
      else
      end
      append_21(flags.cflags, sh_split(cflags))
      append_21(flags.link, sh_split(libs))
    else
      table.insert(flags.link, ("-l" .. lib))
    end
  end
  return flags
end
local function build_suite(suite, cc, cflags, covflags, unitdir, sf_test_o, verbose_3f)
  local pkg = suite_pkg_flags(suite)
  local use_cflags
  local _58_
  do
    local tbl_26_ = {}
    local i_27_ = 0
    for _, f in ipairs(cflags) do
      local val_28_ = f
      if (nil ~= val_28_) then
        i_27_ = (i_27_ + 1)
        tbl_26_[i_27_] = val_28_
      else
      end
    end
    _58_ = tbl_26_
  end
  use_cflags = append_21(_58_, pkg.cflags)
  local objs = {sf_test_o}
  if suite.stub_log then
    local stub_c = (paths.tests .. "/stubs/log_stub.c")
    local stub_o = (unitdir .. "/log_stub.o")
    compile_obj(cc, use_cflags, covflags, stub_c, stub_o, verbose_3f, {(paths.root .. "/src/log/log.h")})
    table.insert(objs, stub_o)
  else
  end
  for _, rel in ipairs(suite.srcs) do
    local src = (paths.src .. "/" .. rel)
    if not file_exists_3f(src) then
      die((suite.test_c .. ": src=" .. rel .. " missing"))
    else
    end
    local obj = (unitdir .. "/" .. rel:gsub("/", "_") .. ".o")
    compile_obj(cc, use_cflags, covflags, src, obj, verbose_3f)
    table.insert(objs, obj)
  end
  do
    local test_o = (unitdir .. "/" .. suite.name .. ".o")
    compile_obj(cc, use_cflags, covflags, suite.test_c, test_o, verbose_3f, {(paths.tests .. "/sf_test.h")})
    table.insert(objs, test_o)
  end
  local binpath = (unitdir .. "/" .. suite.name)
  if newer_3f(binpath, objs) then
    local cmd = {cc}
    append_21(cmd, covflags)
    table.insert(cmd, "-o")
    table.insert(cmd, binpath)
    append_21(cmd, objs)
    append_21(cmd, pkg.link)
    run_cmd(cmd, verbose_3f)
  else
  end
  return binpath
end
local function run_bin(binpath)
  local outf = os.tmpname()
  local errf = os.tmpname()
  local cmd = string.format("%s >%s 2>%s", shell_quote(binpath), shell_quote(outf), shell_quote(errf))
  local ok, what, code = os.execute(cmd)
  local stdout = (read_file(outf) or "")
  local stderr = (read_file(errf) or "")
  os.remove(outf)
  os.remove(errf)
  local _63_
  if ok then
    _63_ = 0
  else
    if ((what == "exit") and (type(code) == "number")) then
      _63_ = code
    else
      _63_ = 1
    end
  end
  return _63_, stdout, stderr
end
local function cover_obj_for(suite, unitdir)
  if (#suite.srcs == 0) then
    return (unitdir .. "/" .. suite.name .. ".o")
  else
    local primary
    local _66_
    do
      local hit = nil
      for _, rel in ipairs(suite.srcs) do
        if hit then break end
        if ((rel == suite.cover) or (basename(rel) == basename(suite.cover))) then
          hit = rel
        else
          hit = nil
        end
      end
      _66_ = hit
    end
    primary = (_66_ or suite.srcs[1])
    return (unitdir .. "/" .. primary:gsub("/", "_") .. ".o")
  end
end
local function parse_sf_result(stdout)
  local counts = {pass = 0, fail = 0}
  for line in stdout:gmatch("[^\n]+") do
    if (line:sub(1, 15) == "sf_test_result ") then
      for kv in line:gmatch("%S+") do
        local case_69_, case_70_ = kv:match("^(%w+)=(%-?%d+)$")
        if ((nil ~= case_69_) and (nil ~= case_70_)) then
          local k = case_69_
          local v = case_70_
          if (k == "pass") then
            counts.pass = (tonumber(v) or 0)
          elseif (k == "fail") then
            counts.fail = (tonumber(v) or 0)
          else
          end
        else
          local _ = case_69_
        end
      end
    else
    end
  end
  return counts
end
local function build_all(suites, cc, cflags, covflags, unitdir, sf_test_o, verbose_3f)
  local built = {bins = {}, ["cov-objs"] = {}}
  for _, suite in ipairs(suites) do
    built.bins[suite.name] = build_suite(suite, cc, cflags, covflags, unitdir, sf_test_o, verbose_3f)
    built["cov-objs"][suite.name] = cover_obj_for(suite, unitdir)
  end
  return built
end
local function main()
  local opts = (os.getenv("OPTS") or "")
  local flags = parse_opts(opts)
  local cc = (os.getenv("CC") or "cc")
  local cflags = sh_split((os.getenv("CFLAGS_TEST") or "-std=c23 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L"))
  local unitdir
  local _74_
  if flags.cover then
    _74_ = "/build/unit-cover"
  else
    _74_ = "/build/unit"
  end
  unitdir = (paths.root .. _74_)
  local covflags
  if flags.cover then
    covflags = {"--coverage"}
  else
    covflags = {}
  end
  append_21(cflags, {"-I", paths["stub-inc"], "-I", paths.tests, "-I", paths.src})
  ensure_dir(unitdir)
  if flags.verbose then
    ffi.C.setenv("SF_TEST_VERBOSE", "1", 1)
  else
    ffi.C.unsetenv("SF_TEST_VERBOSE")
  end
  local suites = discover()
  if (#suites == 0) then
    die("tests/drive.lua: no *_test.c suites found")
  else
  end
  local sf_test_o = (unitdir .. "/sf_test.o")
  local ok0, err0 = pcall(compile_obj, cc, cflags, covflags, (paths.tests .. "/sf_test.c"), sf_test_o, flags.verbose, {(paths.tests .. "/sf_test.h")})
  if not ok0 then
    io.stderr:write(("tests/drive.lua: build failed: " .. tostring(err0) .. "\n"))
    os.exit(2)
  else
  end
  local ok1, err_or_built = pcall(build_all, suites, cc, cflags, covflags, unitdir, sf_test_o, flags.verbose)
  if not ok1 then
    io.stderr:write(("tests/drive.lua: build failed: " .. tostring(err_or_built) .. "\n"))
    os.exit(2)
  else
  end
  local built = err_or_built
  local cov = {measured = {}, ["folder-fail"] = {}}
  local tally = {["ok-files"] = 0, ["fail-files"] = 0, ["ok-funcs"] = 0, ["fail-funcs"] = 0, ["prev-folder"] = nil}
  if flags.cover then
    for _, gcda in ipairs(popen_lines(("find " .. shell_quote(unitdir) .. " -maxdepth 1 -name '*.gcda' 2>/dev/null"))) do
      os.remove(gcda)
    end
  else
  end
  local function emit_folder(folder)
    if flags.cover then
      local avg, hit, n = folder_stats(folder, cov.measured)
      local note = string.format("coverage: %.1f%% of lines / %d out of %d files", avg, hit, n)
      local failed_3f = cov["folder-fail"][folder]
      local _82_
      if failed_3f then
        _82_ = "FAIL"
      else
        _82_ = "ok"
      end
      return print_row(failed_3f, _82_, folder, "", note)
    else
      return nil
    end
  end
  for _, suite in ipairs(suites) do
    if (tally["prev-folder"] and (suite.folder ~= tally["prev-folder"])) then
      emit_folder(tally["prev-folder"])
    else
    end
    tally["prev-folder"] = suite.folder
    local t0 = now()
    local rc, stdout, stderr = run_bin(built.bins[suite.name])
    local elapsed = string.format("%.3fs", (now() - t0))
    local counts = parse_sf_result(stdout)
    if (rc == 0) then
      local extra
      if not flags.cover then
        extra = ""
      else
        local pct = (gcov_pct(unitdir, built["cov-objs"][suite.name], suite.cover) or 0.0)
        local prev = cov.measured[suite.cover]
        if ((prev == nil) or (pct > prev)) then
          cov.measured[suite.cover] = pct
        else
        end
        extra = string.format("coverage: %.1f%% of lines", pct)
      end
      if (flags.verbose and (stderr ~= "")) then
        io.stderr:write(stderr)
      else
      end
      print_row(false, "ok", suite.cover, elapsed, extra)
      tally["ok-files"] = (tally["ok-files"] + 1)
      tally["ok-funcs"] = (tally["ok-funcs"] + counts.pass)
      tally["fail-funcs"] = (tally["fail-funcs"] + counts.fail)
    else
      local extra
      if not flags.cover then
        extra = ""
      else
        if (cov.measured[suite.cover] == nil) then
          cov.measured[suite.cover] = 0.0
        else
        end
        extra = "coverage: 0.0% of lines"
      end
      print_row(true, "FAIL", suite.cover, elapsed, extra)
      if (stderr ~= "") then
        io.stderr:write(stderr)
      else
      end
      tally["fail-files"] = (tally["fail-files"] + 1)
      tally["ok-funcs"] = (tally["ok-funcs"] + counts.pass)
      tally["fail-funcs"] = (tally["fail-funcs"] + counts.fail)
      cov["folder-fail"][suite.folder] = true
    end
  end
  if tally["prev-folder"] then
    emit_folder(tally["prev-folder"])
  else
  end
  io.stdout:write("--------------------------------------------------------------------------------\n")
  local ok_extra
  if not flags.cover then
    ok_extra = ""
  else
    local avg, hit, n = overall_stats(cov.measured)
    ok_extra = string.format("coverage: %.1f%% of lines / %d out of %d files", avg, hit, n)
  end
  print_row(false, "OK", string.format("%d files / %d functions", tally["ok-files"], tally["ok-funcs"]), "", ok_extra)
  if (tally["fail-files"] > 0) then
    print_row(true, "FAIL", string.format("%d files / %d functions", tally["fail-files"], tally["fail-funcs"]), "")
  else
  end
  local function _96_()
    if (tally["fail-files"] > 0) then
      return 1
    else
      return 0
    end
  end
  return os.exit(_96_())
end
return main()
