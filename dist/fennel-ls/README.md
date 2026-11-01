# fennel-ls docset for stonefly

Completion / hover for the compositor Fennel API (`sf.*`) via
[fennel-ls](https://git.sr.ht/~xerool/fennel-ls) external libraries
(same idea as [fennel-ls-nvim-docs](https://git.sr.ht/~micampe/fennel-ls-nvim-docs)).

## Install

```sh
mkdir -p "${XDG_DATA_HOME:-$HOME/.local/share}/fennel-ls/docsets"
cp dist/fennel-ls/docsets/sf.lua \
  "${XDG_DATA_HOME:-$HOME/.local/share}/fennel-ls/docsets/"
```

Enable in the config dir (shipped as `src/config/default/flsproject.fnl`,
also written by `stonefly --write-config`):

```fennel
{:lua-version :lua5.1
 :libraries {:sf true}}
```

Restart the LSP client. Needs fennel-ls ≥ 0.2.0.

## Source of truth

Hand-maintained from `src/config/config_lua.c` (`push_sf_table`, `opt_*`
tables, `l_bind` / `l_tap` / `l_scratchpad` / `l_window_rule`, …) and
`src/config/action.c` action names. Update `docsets/sf.lua` when the Lua
API changes.
