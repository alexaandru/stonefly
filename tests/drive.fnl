;;;; Discover, build, run, and cover stonefly unit suites.
;;;;
;;;; Source of truth for the unit-test driver. Compile to committed Lua with:
;;;;
;;;;   make drive-lua
;;;;
;;;; Runtime (`make test` / APK check) uses only tests/drive.lua — Fennel is
;;;; not on that path.
;;;;
;;;; Suites are tests/**/*_test.c with a pragma near the top:
;;;;
;;;;   /* sf-suite cover=util/str.c src=util/str.c stub=log libs=m */
;;;;
;;;;   cover=  path under src/ shown in the report (required)
;;;;   src=    comma-separated extra .c files under src/ to link (optional)
;;;;   stub=   log  → also compile tests/stubs/log_stub.c (optional)
;;;;   libs=   m            → -lm
;;;;           pixman-1     → pkg-config --cflags --libs pixman-1

;;;; LuaJIT FFI is only a thin POSIX bridge — not general “call C from tests”:
;;;;   • clock_gettime(CLOCK_MONOTONIC) — wall-clock suite timing (os.clock is
;;;;     process CPU time and skips the child test binary; Python used
;;;;     time.perf_counter).
;;;;   • setenv / unsetenv — pass SF_TEST_VERBOSE into spawned *_test binaries
;;;;     (stock Lua has no env mutators).
;;;; (lua "-- …") below keeps the same note in the committed drive.lua.)
(lua "-- FFI: clock_gettime for wall-clock suite timing (os.clock skips children);
-- setenv/unsetenv for SF_TEST_VERBOSE on spawned *_test binaries.")
(local ffi (require :ffi))

(ffi.cdef "typedef long time_t;
typedef struct { time_t tv_sec; long tv_nsec; } timespec;
int clock_gettime(int clk_id, timespec *tp);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);")

(local CLOCK_MONOTONIC 1)

(fn now []
  (let [ts (ffi.new :timespec)]
    (if (not= (ffi.C.clock_gettime CLOCK_MONOTONIC ts) 0)
        (os.clock)
        (+ (tonumber ts.tv_sec) (* (tonumber ts.tv_nsec) 1e-9)))))

(fn script-dir []
  (let [raw (. (debug.getinfo 1 :S) :source)
        src (if (= (raw:sub 1 1) "@") (raw:sub 2) raw)]
    (or (src:match "^(.*)/[^/]+$") ".")))

(local paths
  (let [root (let [dir (script-dir)]
               (or (dir:match "^(.*)/[^/]+$") (.. dir "/..")))
        tests (.. root "/tests")]
    {:root root
     :tests tests
     :src (.. root "/src")
     :stub-inc (.. tests "/stubs/include")}))

(fn die [msg]
  (io.stderr:write (.. msg "\n"))
  (os.exit 1))

(fn shell-quote [s]
  (.. "'" (: (tostring s) :gsub "'" "'\\''") "'"))

(fn file-exists? [path]
  (case (io.open path :r)
    f (do
        (f:close)
        true)
    _ false))

(fn read-head [path n]
  (case (io.open path :r)
    f (let [data (or (f:read n) "")]
        (f:close)
        data)
    _ (die (.. path ": cannot read"))))

(fn read-file [path]
  (case (io.open path :r)
    f (let [data (f:read "*a")]
        (f:close)
        data)
    _ nil))

(fn split-ws [s]
  (icollect [tok (s:gmatch "%S+")] tok))

(fn split-comma [s]
  (icollect [part (: (.. s ",") :gmatch "([^,]*),")]
    (if (not= part "") part)))

(fn basename [path]
  (or (path:match "([^/]+)$") path))

(fn dirname [path]
  (or (path:match "^(.*)/[^/]+$") "."))

(fn ensure-dir [path]
  (if (not (os.execute (.. "mkdir -p " (shell-quote path))))
      (die (.. "mkdir failed: " path))))

(fn popen-lines [cmd]
  (case (io.popen cmd)
    p (let [lines (icollect [line (p:lines)] line)]
        (p:close)
        lines)
    _ (die (.. "popen failed: " cmd))))

(fn sh-split [s]
  "Minimal shlex-like split for pkg-config output."
  (local out [])
  (var i 1)
  (let [n (length s)]
    (while (<= i n)
      (while (and (<= i n) (: (s:sub i i) :match "%s"))
        (set i (+ i 1)))
      (if (<= i n)
          (let [c (s:sub i i)]
            (if (or (= c "\"") (= c "'"))
                (do
                  (var j (+ i 1))
                  (local buf [])
                  (while (and (<= j n) (not= (s:sub j j) c))
                    (table.insert buf (s:sub j j))
                    (set j (+ j 1)))
                  (table.insert out (table.concat buf))
                  (set i (+ j 1)))
                (do
                  (var j i)
                  (while (and (<= j n) (not (: (s:sub j j) :match "%s")))
                    (set j (+ j 1)))
                  (table.insert out (s:sub i (- j 1)))
                  (set i j)))))))
  out)

(fn parse-suite [path]
  (let [head (read-head path 4096)
        body (head:match "/%*%s*sf%-suite%s+(.-)%s*%*/")]
    (if (not body)
        (die (.. path ": missing /* sf-suite … */ pragma")))
    (local fields {:cover nil :srcs [] :stub-log false :libs []})
    (each [_ tok (ipairs (split-ws body))]
      (let [(k v) (tok:match "^([^=]+)=(.*)$")]
        (if (not k)
            (die (.. path ": bad sf-suite token " tok)))
        (case k
          :cover (set fields.cover v)
          :src (set fields.srcs (split-comma v))
          :stub (each [_ s (ipairs (split-comma v))]
                  (if (= s :log)
                      (set fields.stub-log true)))
          :libs (set fields.libs (split-comma v))
          _ (die (.. path ": unknown sf-suite key " k)))))
    (if (not fields.cover)
        (die (.. path ": sf-suite needs cover=")))
    (let [rel (path:sub (+ (length paths.tests) 2))
          name (-> rel (: :gsub "%.c$" "") (: :gsub "/" "_"))
          folder (.. (or (fields.cover:match "^([^/]+)/") fields.cover) "/")]
      {:test_c path
       :cover fields.cover
       :srcs fields.srcs
       :stub_log fields.stub-log
       :libs fields.libs
       :name name
       :folder folder})))

(fn discover []
  (let [found (popen-lines (.. "find " (shell-quote paths.tests)
                               " -type f -name '*_test.c' ! -name sf_test.c | sort"))]
    (icollect [_ p (ipairs found)]
      (parse-suite p))))

(fn parse-opts [opts]
  (local flags {:verbose false :cover false})
  (each [opt (: (.. opts ",") :gmatch "([^,]*),")]
    (case (or (opt:match "^%s*(.-)%s*$") "")
      :verbose (set flags.verbose true)
      :cover (set flags.cover true)
      "" nil
      bad (die (.. "tests/drive.lua: unknown OPTS entry: " bad))))
  flags)

(fn color-enabled? []
  (or (= (os.getenv :SF_TEST_COLOR) :1)
      (= (os.execute "test -t 2") true)))

(fn print-row [to-err status path elapsed extra]
  (let [elapsed (or elapsed "")
        extra (or extra "")
        color? (color-enabled?)
        red (if color? "\027[31m" "")
        reset (if color? "\027[0m" "")
        label (if (and (= status :FAIL) color?)
                  (.. red :FAIL reset)
                  (string.format "%-4s" status))
        row (string.format "%s  %-24s%8s" label path elapsed)
        row (if (not= extra "") (.. row "  " extra) row)
        out (if to-err io.stderr io.stdout)]
    (out:write (.. row "\n"))))

(fn newer? [target inputs]
  (or (not (file-exists? target))
      (accumulate [need false _ inp (ipairs inputs) &until need]
        (os.execute (.. "test " (shell-quote inp) " -nt " (shell-quote target))))))

(fn run-cmd [argv verbose?]
  (let [cmdline (table.concat (icollect [_ a (ipairs argv)] (shell-quote a)) " ")]
    (if verbose?
        (io.stderr:write (.. "+ " (table.concat argv " ") "\n")))
    (let [(ok what code) (os.execute cmdline)]
      (if (not ok)
          (error (string.format "command failed (%s %s): %s"
                                (tostring what) (tostring code) cmdline)
                 0)))))

(fn append! [dst src]
  (each [_ x (ipairs src)]
    (table.insert dst x))
  dst)

(fn compile-obj [cc cflags covflags src obj verbose? extra-inputs]
  (let [inputs (append! [src] (or extra-inputs []))]
    (if (newer? obj inputs)
        (do
          (ensure-dir (dirname obj))
          (local cmd [cc])
          (append! cmd cflags)
          (append! cmd covflags)
          (table.insert cmd :-c)
          (table.insert cmd src)
          (table.insert cmd :-o)
          (table.insert cmd obj)
          (run-cmd cmd verbose?)))))

(fn gcov-pct [unitdir obj want]
  (case (io.popen (.. "cd " (shell-quote unitdir)
                      " && gcov -n " (shell-quote (basename obj))
                      " 2>/dev/null"))
    p (do
        (local st {:cur nil :pct nil})
        (each [line (p:lines)]
          (case (line:match "^File '([^']+)'")
            f (set st.cur f)
            _ (case (line:match "Lines executed:([0-9.]+)%% of [0-9]+")
                n (if st.cur
                      (let [path (if (= (st.cur:sub 1 4) :src/)
                                     (st.cur:sub 5)
                                     st.cur)]
                        (if (or (= path want)
                                (= (st.cur:sub (- (length want))) want))
                            (set st.pct (tonumber n))))))))
        (p:close)
        st.pct)
    _ nil))

(fn list-src-tree []
  (popen-lines (.. "find " (shell-quote paths.src)
                   " -type f \\( -name '*.c' -o -name '*.h' \\) | sort")))

(fn avg-hit [vals]
  (local st {:sum 0.0 :hit 0})
  (each [_ v (ipairs vals)]
    (set st.sum (+ st.sum v))
    (if (> v 0.0)
        (set st.hit (+ st.hit 1))))
  (let [n (length vals)]
    (values (if (> n 0) (/ st.sum n) 0.0) st.hit n)))

(fn folder-stats [folder measured]
  (avg-hit (icollect [_ p (ipairs (list-src-tree))]
             (let [rel (p:sub (+ (length paths.src) 2))]
               (if (= (.. (or (rel:match "^([^/]+)/") rel) "/") folder)
                   (or (. measured rel) 0.0))))))

(fn overall-stats [measured]
  (avg-hit (icollect [_ p (ipairs (list-src-tree))]
             (or (. measured (p:sub (+ (length paths.src) 2))) 0.0))))

(fn pkg-config [flag pkg]
  (case (io.popen (.. "pkg-config " flag " " pkg " 2>/dev/null"))
    p (let [out (or (p:read "*a") "")
            ok (select 1 (p:close))]
        (values ok out))
    _ (values false "")))

(fn suite-pkg-flags [suite]
  (local flags {:cflags [] :link []})
  (each [_ lib (ipairs suite.libs)]
    (if (= lib :pixman-1)
        (let [(cok cflags) (pkg-config :--cflags :pixman-1)
              (lok libs) (pkg-config :--libs :pixman-1)]
          (if (or (not cok) (not lok))
              (die (.. suite.test_c ": pkg-config pixman-1 failed")))
          (append! flags.cflags (sh-split cflags))
          (append! flags.link (sh-split libs)))
        (table.insert flags.link (.. "-l" lib))))
  flags)

(fn build-suite [suite cc cflags covflags unitdir sf-test-o verbose?]
  (let [pkg (suite-pkg-flags suite)
        use-cflags (append! (icollect [_ f (ipairs cflags)] f) pkg.cflags)
        objs [sf-test-o]]
    (if suite.stub_log
        (let [stub-c (.. paths.tests "/stubs/log_stub.c")
              stub-o (.. unitdir "/log_stub.o")]
          (compile-obj cc use-cflags covflags stub-c stub-o verbose?
                       [(.. paths.root "/src/log/log.h")])
          (table.insert objs stub-o)))
    (each [_ rel (ipairs suite.srcs)]
      (let [src (.. paths.src "/" rel)]
        (if (not (file-exists? src))
            (die (.. suite.test_c ": src=" rel " missing")))
        (let [obj (.. unitdir "/" (rel:gsub "/" "_") ".o")]
          (compile-obj cc use-cflags covflags src obj verbose?)
          (table.insert objs obj))))
    (let [test-o (.. unitdir "/" suite.name ".o")]
      (compile-obj cc use-cflags covflags suite.test_c test-o verbose?
                   [(.. paths.tests "/sf_test.h")])
      (table.insert objs test-o))
    (let [binpath (.. unitdir "/" suite.name)]
      (if (newer? binpath objs)
          (do
            (local cmd [cc])
            (append! cmd covflags)
            (table.insert cmd :-o)
            (table.insert cmd binpath)
            (append! cmd objs)
            (append! cmd pkg.link)
            (run-cmd cmd verbose?)))
      binpath)))

(fn run-bin [binpath]
  (let [outf (os.tmpname)
        errf (os.tmpname)
        cmd (string.format "%s >%s 2>%s"
                           (shell-quote binpath)
                           (shell-quote outf)
                           (shell-quote errf))
        (ok what code) (os.execute cmd)
        stdout (or (read-file outf) "")
        stderr (or (read-file errf) "")]
    (os.remove outf)
    (os.remove errf)
    (values (if ok 0
                (if (and (= what :exit) (= (type code) :number)) code 1))
            stdout stderr)))

(fn cover-obj-for [suite unitdir]
  (if (= (length suite.srcs) 0)
      (.. unitdir "/" suite.name ".o")
      (let [primary (or (accumulate [hit nil _ rel (ipairs suite.srcs) &until hit]
                          (if (or (= rel suite.cover)
                                  (= (basename rel) (basename suite.cover)))
                              rel))
                        (. suite.srcs 1))]
        (.. unitdir "/" (primary:gsub "/" "_") ".o"))))

(fn parse-sf-result [stdout]
  (local counts {:pass 0 :fail 0})
  (each [line (stdout:gmatch "[^\n]+")]
    (if (= (line:sub 1 15) "sf_test_result ")
        (each [kv (line:gmatch "%S+")]
          (case (kv:match "^(%w+)=(%-?%d+)$")
            (k v) (case k
                    :pass (set counts.pass (or (tonumber v) 0))
                    :fail (set counts.fail (or (tonumber v) 0)))
            _ nil))))
  counts)

(fn build-all [suites cc cflags covflags unitdir sf-test-o verbose?]
  (local built {:bins {} :cov-objs {}})
  (each [_ suite (ipairs suites)]
    (tset built.bins suite.name
          (build-suite suite cc cflags covflags unitdir sf-test-o verbose?))
    (tset built.cov-objs suite.name (cover-obj-for suite unitdir)))
  built)

(fn main []
  (let [opts (or (os.getenv :OPTS) "")
        flags (parse-opts opts)
        cc (or (os.getenv :CC) :cc)
        cflags (sh-split (or (os.getenv :CFLAGS_TEST)
                             "-std=c23 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L"))
        unitdir (.. paths.root (if flags.cover "/build/unit-cover" "/build/unit"))
        covflags (if flags.cover ["--coverage"] [])]
    (append! cflags [:-I paths.stub-inc :-I paths.tests :-I paths.src])
    (ensure-dir unitdir)
    (if flags.verbose
        (ffi.C.setenv :SF_TEST_VERBOSE :1 1)
        (ffi.C.unsetenv :SF_TEST_VERBOSE))
    (let [suites (discover)]
      (if (= (length suites) 0)
          (die "tests/drive.lua: no *_test.c suites found"))
      (let [sf-test-o (.. unitdir "/sf_test.o")
            (ok0 err0) (pcall compile-obj cc cflags covflags
                              (.. paths.tests "/sf_test.c") sf-test-o flags.verbose
                              [(.. paths.tests "/sf_test.h")])]
        (if (not ok0)
            (do
              (io.stderr:write (.. "tests/drive.lua: build failed: "
                                   (tostring err0) "\n"))
              (os.exit 2)))
        (let [(ok1 err-or-built) (pcall build-all suites cc cflags covflags
                                        unitdir sf-test-o flags.verbose)]
          (if (not ok1)
              (do
                (io.stderr:write (.. "tests/drive.lua: build failed: "
                                     (tostring err-or-built) "\n"))
                (os.exit 2)))
          (let [built err-or-built
                cov {:measured {} :folder-fail {}}
                tally {:ok-files 0
                       :fail-files 0
                       :ok-funcs 0
                       :fail-funcs 0
                       :prev-folder nil}]
            (if flags.cover
                (each [_ gcda (ipairs (popen-lines (.. "find " (shell-quote unitdir)
                                                      " -maxdepth 1 -name '*.gcda' 2>/dev/null")))]
                  (os.remove gcda)))
            (fn emit-folder [folder]
              (if flags.cover
                  (let [(avg hit n) (folder-stats folder cov.measured)
                        note (string.format "coverage: %.1f%% of lines / %d out of %d files"
                                            avg hit n)
                        failed? (. cov.folder-fail folder)]
                    (print-row failed? (if failed? :FAIL :ok) folder "" note))))
            (each [_ suite (ipairs suites)]
              (if (and tally.prev-folder (not= suite.folder tally.prev-folder))
                  (emit-folder tally.prev-folder))
              (set tally.prev-folder suite.folder)
              (let [t0 (now)
                    (rc stdout stderr) (run-bin (. built.bins suite.name))
                    elapsed (string.format "%.3fs" (- (now) t0))
                    counts (parse-sf-result stdout)]
                (if (= rc 0)
                    (let [extra (if (not flags.cover) ""
                                    (let [pct (or (gcov-pct unitdir
                                                            (. built.cov-objs suite.name)
                                                            suite.cover)
                                                  0.0)
                                          prev (. cov.measured suite.cover)]
                                      (if (or (= prev nil) (> pct prev))
                                          (tset cov.measured suite.cover pct))
                                      (string.format "coverage: %.1f%% of lines" pct)))]
                      (if (and flags.verbose (not= stderr ""))
                          (io.stderr:write stderr))
                      (print-row false :ok suite.cover elapsed extra)
                      (set tally.ok-files (+ tally.ok-files 1))
                      (set tally.ok-funcs (+ tally.ok-funcs counts.pass))
                      (set tally.fail-funcs (+ tally.fail-funcs counts.fail)))
                    (let [extra (if (not flags.cover) ""
                                    (do
                                      (if (= (. cov.measured suite.cover) nil)
                                          (tset cov.measured suite.cover 0.0))
                                      "coverage: 0.0% of lines"))]
                      (print-row true :FAIL suite.cover elapsed extra)
                      (if (not= stderr "")
                          (io.stderr:write stderr))
                      (set tally.fail-files (+ tally.fail-files 1))
                      (set tally.ok-funcs (+ tally.ok-funcs counts.pass))
                      (set tally.fail-funcs (+ tally.fail-funcs counts.fail))
                      (tset cov.folder-fail suite.folder true)))))
            (if tally.prev-folder
                (emit-folder tally.prev-folder))
            (io.stdout:write "--------------------------------------------------------------------------------\n")
            (let [ok-extra (if (not flags.cover) ""
                               (let [(avg hit n) (overall-stats cov.measured)]
                                 (string.format "coverage: %.1f%% of lines / %d out of %d files"
                                                avg hit n)))]
              (print-row false :OK
                         (string.format "%d files / %d functions"
                                        tally.ok-files tally.ok-funcs)
                         "" ok-extra)
              (if (> tally.fail-files 0)
                  (print-row true :FAIL
                             (string.format "%d files / %d functions"
                                            tally.fail-files tally.fail-funcs)
                             ""))
              (os.exit (if (> tally.fail-files 0) 1 0)))))))))

(main)
