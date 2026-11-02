# Agent Notes

## Style

TODO: CERT C? LINUX KERNEL C? GNOME C Coding styles...!?!? So many to choose from.

## Lint

Directly depends on selected style.

### Known False Positives

**`_POSIX_C_SOURCE` identifier warning (pi-lens LSP check)**

Symptom: Pi-lens flags `#define _POSIX_C_SOURCE 200809L` as a reserved identifier namespace violation.

Why it's false: `_POSIX_C_SOURCE` is a standard POSIX feature-test macro (POSIX.1-2008 section 2.2.1.5) that **must be defined by user code** to enable POSIX features at compilation. It appears identically in all 16 `.c` files in stonefly and compiles correctly in the abuild sandbox.

Workaround: A `.pi-lens.json` file exists at the project root to suppress this warning. If LSP diagnostics still appear in the pi TUI during development, they are harmless and safe to ignore - they do not affect the build.

Note: This warning comes from pi-lens's LSP analyzer (clangd) applying C standard reserved identifier rules without understanding POSIX's special carve-out for feature-test macros.

## MATRIX.md

Always keep [`MATRIX.md`](MATRIX.md) up to date after completing a change, fix, feature, or notable diagnosis. Update the matching rows (status, notes, peeble/stonefly columns) in the same turn as the work — do not leave MATRIX stale for a later cleanup pass.

## Build environment

DO NOT attempt to lookup libraries/APIs on host!!! For two reasons:

1. you almost ALWAYS run in a sandbox and
2. they don't exist on host anyway, the entire build is encapsulated in abuild.
