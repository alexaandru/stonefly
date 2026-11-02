;; Fennel shim for stonefly — adapted from fennel-nvim/fnl/fennel-shim/init.fnl.
;; All shim logic is Fennel. plugin/fennel.lua only: require fennel + dofile this.

(local fennel (require :fennel))

(local cache-dir (.. (stonefly.stdpath :cache) :/fennel))
(os.execute (.. "mkdir -p '" (cache-dir:gsub "'" "'\\''") "'"))

(local cache {:cache {}})

(fn cache.path [file]
  (var key (file:gsub "[^%w]+" "_"))
  (when (> (length key) 200)
    (set key (key:sub -200)))
  (.. cache-dir "/" key :.bc))

(fn cache.compile [file]
  (let [f (assert (io.open file :r))
        src (f:read :*all)
        _ (f:close)
        lua-src (fennel.compileString src {:filename file})
        chunk (assert (load lua-src (.. "@" file)))]
    (string.dump chunk)))

(fn cache.fetch [file]
  (?. cache.cache file))

(fn cache.write-disk [file bytecode mtime]
  (let [out (io.open (cache.path file) :wb)]
    (when out
      (let [sec (% mtime 4294967296)]
        (out:write (string.char (% sec 256)
                                (% (math.floor (/ sec 256)) 256)
                                (% (math.floor (/ sec 65536)) 256)
                                (% (math.floor (/ sec 16777216)) 256)))
        (out:write bytecode)
        (out:close)))))

(fn cache.read-disk [file mtime]
  (let [cf (io.open (cache.path file) :rb)]
    (when cf
      (let [meta (cf:read 4)
            body (cf:read :*all)]
        (cf:close)
        (when (and meta (= (length meta) 4) body (> (length body) 0))
          (let [(b1 b2 b3 b4) (meta:byte 1 4)
                sec (+ b1 (* b2 256) (* b3 65536) (* b4 16777216))]
            (when (= sec (% mtime 4294967296))
              body)))))))

(fn cache.set [file sec]
  (let [data (cache.compile file)]
    (tset cache.cache file {: data : sec})
    (cache.write-disk file data sec)
    data))

(fn cache.fetch-or-set [file]
  (let [sec (or (stonefly._fs_mtime file) 0)
        mem (cache.fetch file)]
    (if (and mem (not (< mem.sec sec)))
        (values mem.data :hit)
        (let [disk (cache.read-disk file sec)]
          (if disk
              (do
                (tset cache.cache file {:data disk : sec})
                (values disk :hit))
              (values (cache.set file sec) :miss))))))

(fn fennel-loader [file]
  (let [(bytecode _hit) (cache.fetch-or-set file)
        loader (assert (load bytecode (.. "@" file)))]
    (loader)))

(fn find-module [modname]
  (let [name (modname:gsub "%." "/")
        candidates [(.. name :.fnl)
                    (.. name :/init.fnl)
                    (.. :fnl/ name :.fnl)
                    (.. :fnl/ name :/init.fnl)]]
    (accumulate [found nil _ rel (ipairs candidates) :until found]
      (stonefly.runtime_file rel))))

;; Searcher: return nil on miss so later loaders can run.
;; Appended after Lua path searchers — never ship lua/X.lua and fnl/X.fnl
;; for the same module name X, or Lua wins.
(fn load-lib [modname]
  (case (find-module modname)
    file (fn [] (fennel-loader file))
    _ nil))

(table.insert package.loaders load-lib)

(tset package.preload :stonefly.fennel
      (fn []
        {:dofile (fn [path] (fennel-loader path))}))

{:loader fennel-loader :find find-module}
