# OTP (awbus)

Noctalia port of peeble’s `/otp` launcher plugin. Search OTP accounts from
[`awbus`](https://github.com/alexaandru/awbus) and generate a code (awbus copies
it to the clipboard).

| Field | Value |
| --- | --- |
| ID | `stonefly/otp` |
| Entry | `otp` (full id `stonefly/otp:otp`) |
| Prefix | `/otp` (with default `provider_prefix`) |
| Global search | yes (`include_in_global_search = true`) |

## Behavior

Matches peeble’s `launcher_plugin("/otp", …, {global: true})`:

1. Runs `awbus list-otp`.
2. Shows each account with subtitle `Generate OTP Code for <name>`.
3. Icon is the lowercased first path segment (`Github` → `github`, `AWS/FireMedia` → `aws`).
4. Ranks with Noctalia’s `noctalia.fuzzyScore`.
5. On activate, runs `awbus otp code <name>` detached (same as peeble’s `spawn`).

Use `/otp`, `/otp git`, or a bare query like `github` (global search).

Unprefixed global opt-in only runs after **at least two characters** (Noctalia
policy for all prefixed providers that opt into global search).

## Requirements

- `awbus` on `PATH` (`awbus list-otp`, `awbus otp code <name>`)
- Noctalia with plugin API ≥ 24 (argument-array `runAsync`)

## Install

Prefer a **path source under your config tree** (dotfiles-friendly). Git-sourced
plugins are exported into `$XDG_STATE_HOME/noctalia/…`; keeping this one in
config avoids that.

Then **enable** it (discovered ≠ enabled).

### A. Path source in config (recommended)

Layout Noctalia expects (a **parent** directory of plugin folders):

```text
~/.config/noctalia/          ← or your managed config dir
  config.toml
  otp/
    plugin.toml
    otp.luau
    README.md
```

**Important:** a declared `[[plugins.source]]` list **replaces** Noctalia’s
default sources. Keep the built-in `official` and `community` git sources in
that list (or you will only see your path plugins). Safer: add the path source
via IPC / Settings (additive) instead of rewriting the whole array by hand.

```toml
[plugins]
enabled = [
  "noctalia/screen_recorder",
  "noctalia/umbriel-companion",
  "noctalia/wallhaven",
  "stonefly/otp",
]
auto_update = "all"

# Defaults — keep these if you declare any [[plugins.source]]
[[plugins.source]]
name     = "official"
kind     = "git"
location = "https://github.com/noctalia-dev/official-plugins"
enabled  = true

[[plugins.source]]
name     = "community"
kind     = "git"
location = "https://github.com/noctalia-dev/community-plugins"
enabled  = true

# Local OTP path source (additive alongside the builtins)
[[plugins.source]]
name     = "stonefly-otp"    # flat name only — see Pitfalls
kind     = "path"
location = "/home/alex/Personal/Config/noctalia"   # parent of otp/
enabled  = true
```

Copy or symlink this directory into that tree:

```sh
# from the stonefly checkout
ln -sfn "$(pwd)/dist/noctalia/launcher/provider/otp" \
  /home/alex/Personal/Config/noctalia/otp
```

Or point `location` at `…/stonefly/dist/noctalia/launcher/provider` and skip the
extra symlink (that directory already contains `otp/`).

Prefer additive registration (does not wipe builtins):

```sh
noctalia msg plugins source add stonefly-otp path /home/alex/Personal/Config/noctalia
noctalia msg plugins enable stonefly/otp
noctalia msg plugins source list
noctalia msg plugins list   # expect stonefly/otp plus noctalia/* from official
```

**Settings → Plugins → Add source** can register the same parent path; then
toggle **OTP** on.

#### Restoring wiped builtins

If `plugins source list` only shows your path source, put the defaults back:

```sh
noctalia msg plugins source add official git https://github.com/noctalia-dev/official-plugins
noctalia msg plugins source add community git https://github.com/noctalia-dev/community-plugins
noctalia msg plugins update official
noctalia msg plugins update community
```

Or re-add the two `[[plugins.source]]` git blocks above in `config.toml` and
`noctalia msg config-reload`.

### B. XDG data local plugins (alternative)

Noctalia also discovers plugins dropped under
`${XDG_DATA_HOME:-$HOME/.local/share}/noctalia/plugins/<name>/`. Fine for a
quick test; less nice if you want the plugin versioned with the rest of your
config.

```sh
mkdir -p "${XDG_DATA_HOME:-$HOME/.local/share}/noctalia/plugins"
ln -sfn /absolute/path/to/stonefly/dist/noctalia/launcher/provider/otp \
  "${XDG_DATA_HOME:-$HOME/.local/share}/noctalia/plugins/otp"
noctalia msg plugins enable stonefly/otp
```

## Global search / prefix overrides

The Launcher **Settings → Providers** UI only lists built-ins (calculator,
emoji, …). Plugin providers are not there.

This plugin already sets `include_in_global_search = true` in `plugin.toml`.
To force or change that from config without editing the plugin:

```toml
[shell.launcher.providers."stonefly/otp:otp"]
prefix = "otp"
global = true
```

## Pitfalls

| Mistake | Result |
| --- | --- |
| Only your path `[[plugins.source]]` in config | Wipes default `official` + `community`; `noctalia/*` plugins disappear |
| `location` = the `otp/` folder itself | Empty catalog; `plugins list` shows nothing from this source |
| Source `name = "stonefly/otp"` (slash) | Source dropped entirely (names must be flat: `a-z0-9._-`) |
| Manifest `id = "otp"` | Rejected; must be `author/plugin` (`stonefly/otp`) |
| `enabled = ["otp"]` | Wrong id; use `stonefly/otp` |
| One-character unprefixed query | Global opt-in ignored until ≥ 2 characters |

## Verify

```sh
noctalia msg plugins list | grep -i otp
awbus list-otp
```

Open the launcher, type `/otp` or at least two letters of an account name,
press Enter — awbus should report success (e.g. `Github OK`) and the code
should be on the clipboard.
