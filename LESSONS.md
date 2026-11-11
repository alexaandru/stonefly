# Lessons

Working notes from peer compositors and from stonefly bugs. Prefer these over
guessing “parity with X.”

## Umbriel (cloned under `./umbriel`)

Noctalia’s compositor sibling: C++/wlroots, layouts **scrolling · dwindle ·
master · stacking**. Stonefly keeps **stacking + columns** only (breadth tax).
Scrolling is niri-inspired; Umbriel’s docs and action catalog are the UX source
of truth for columns. Shell stays out of process (Noctalia).

### Columns are first-class; windows are rows

Scrolling is a strip of fixed-extent columns. A column can hold several windows
stacked vertically. That unit drives consume, tabs, column-maximize, and
focus-within-column. Treating “one window = one column” is too weak for a
usable strip.

Stonefly model (stolen from Umbriel `Column` + `ScrollingLayout::m_columns`):

- `ws->columns` — ordered strip of `struct sf_column`
- each column: `rows` (wl_list), `width_frac`, `width_frac_saved` + `full_width`,
  `top_gap_weight` / `bottom_gap_weight`
- each row (`toplevel`): `column`, `column_link`, `column_row_weight`,
  `column_extent_saved` (primary extent remembered across consume → restored
  on expel)
- pack math: `sf_arrange_math_column_strip` + `sf_arrange_math_column_rows`
- tabs deferred

### Float is sticky membership, not “broke out once”

`window-toggle-floating` only applies on tiling layouts (stacking is already
all-float). Entering a tiling layout re-tiles layout-floated windows; **rule /
user floats stay out**. Visible non-floats must all join the tile set; float is
the only opt-out. Hide/show must not leave unmanaged ghosts under the strip.

### Consume / expel reshape the strip

Default: Mod+, / Mod+. = consume left/right (stack into the neighbor). The
richer pair is consume-or-expel: stack if a neighbor exists, otherwise peel out
of a multi-window column. Also: consume-**from**-left/right (pull the neighbor
in). Move-left/right is a different verb (reorder / new column).

### Maximize is split on purpose

- `toggle-maximize` → **column** fills usable width (still tiled).
- `toggle-maximize-to-edges` → leave gaps/struts/borders (break-out).
- `client_maximize = "column"|"edges"` wires the client button.

Stonefly: Super+m = column-wide in columns; Super+f = edges break-out.
Under columns, edge-max is **transient**: `arrange_max_edges_rejoin` without
sticky `arrange_floating`. Focus change / Tab / focus_dir / Super+m releases
it back into its cell. Marking it `arrange_floating` made Super+Tab walk only
tiles (focus “behind” the overlay) and Super+m look like a pink float.

### Minimize is float-only (Umbriel)

Tiled windows do not minimize into a hole under the strip. A hidden tile must
not keep a ghost packing slot. Stonefly still allows tile minimize but skips
minimized windows in packing; foreign must keep advertising them. Expel keys
off **row count** (any sibling), not visible count — otherwise a sole visible
row stuck with minimized ghosts cannot peel out. Cell-drag INSERT/NEW_COL
indices are **painted** (visible-only) for live spacer reflow and mapped to
structural on commit; raw `sf_column_row_of` as a reflow slot disagrees with
minimized prefixes.

### SoftPlace soft bit vs float-pending

Do not `drop_soft` while a float rule is still possible. Soft false + early
`consider_place` return skips `rules_apply` / join on late title, leaving an
unmanaged cascade under columns. Mirror center hold: keep soft until center
and float identity are decided; when soft is already false, still apply rules
and join/float on identity change.

### Cell-drag home and drop pointers

Home must be a **column pointer** (plus preferred insert index), not a strip
index alone — destroying an earlier column shifts indices and abort restores
into the wrong column. Validate `drop_peer` / `drop_column` on finish (and
clear on unlink of the last row / peer). Mid-drag `break_out` / `arrange_set`
must abort the grab first so finish cannot re-tile an `arrange_floating`
window.

### Scroll policy is “reveal enough,” not always center

On focus, scroll just far enough to show the column. Optional
`center_focused = on_overflow` centers only when the focused column and its
previous neighbor cannot share the viewport. Underfull strips can center as a
strip. Edge-drag during rearrange pans offscreen columns. Stonefly is still
center-on-focus-ish.

### New windows insert beside focus

New column after the focused one, not append-at-MRU-end. Drag shows insertion
preview into a new or existing column.

### Close / focus inside a stacked column

Close a column → nearest survivor; if that column is multi-row, restore the
**MRU row**, not always the first. Missing in stonefly.

### Tabs are a display mode on a column

`column-toggle-tabbed` — same boxes, one visible, tab bar. Optional later.

### Foreign output follows the workspace, not the paint rect

Umbriel pins `zwlr_foreign_toplevel` `output_enter` to the window’s workspace
output. Scrolling columns routinely sit off-screen; geom∩output would
`output_leave` and taskbars drop icons. Same trap as fullscreen mid-configure
0×0 buffers. Stonefly hit this: N windows → fewer taskbar icons only in
columns.

### Scope discipline

Umbriel ships four layouts; stonefly keeps **two** (stacking + columns) for
polish. Shell is Noctalia; “parity with X” is not a reason. Sharpen scrolling
membership / consume / float / drag / foreign before inventing deck/grid.

### Borrowed vs still rough (stonefly)

| Umbriel idea | Stonefly |
|---|---|
| Explicit float membership | `arrange_floating` + rejoin strays |
| Float chrome distinct | pink `border_float_*` |
| Consume L/R (+ expel when stacked) | Super+, / . |
| Column max vs edges max | Super+m / Super+f |
| Columns default | `arrange = columns` |
| Lone visible column fills usable | yes (skip columns with only minimized rows); 2+ use `width_frac` |
| Tiled slide direction | strip-axis L/R (Umbriel windows_in default is **popin**, not closest-slide) |
| Popin / popout (deferred) | Umbriel: fade + presented box `lerp(scale→1)` centered in slot (crop/dest_size). Stonefly v1 path: morph inset→slot + buffer opacity; popout = reverse on hide/ghost. Keep QT/scratch as edge slide. ROADMAP §2 / TODO NEXT #9 |
| Consume / rotate vs minimized | skip minimized-only neighbor columns; pull first visible row |
| Insert after focused column | yes (first-class strip index) |
| First-class Column objects | `sf_column` + `ws->columns` |
| Per-row height weights | `column_row_weight` + end gap weights |
| Consume remembers extent | `column_extent_saved` → expel restore |
| Foreign pin to workspace output | fixed (was geom∩ → taskbar flicker) |
| Drag into column / new column | Super+LMB: cell mid = swap; seam **or inter-row gap** = insert; strip gap = new col |
| Drag live make-room | insert/new-col reflow a real spacer; neighbors shrink; tint on spacer |
| Scroll just-enough / on_overflow | still center-ish |
| Consume-from API | `sf_columns_consume_from` (no default bind yet) |
| Drag edge pan of off-strip columns | missing |
| MRU row after close | missing |
| Column tabs | missing |
| Minimize = float-only policy | tile minimize still allowed |

## Other stonefly lessons

### Arrange vs `layout_sm`

Workspace **arrange** (stacking/columns) owns packing. Per-window `layout_sm`
owns float/max/snap/fullscreen chrome. Do not conflate them: column-max stays
tiled; edges-max breaks out.

### Taskbar / foreign is membership, not visibility

Noctalia’s taskbar keys off foreign-toplevel `output_enter` / state. Off-strip
columns, disabled monocle peers, and mid-fs buffers are still windows. Never
tie foreign output to the current scene box for workspace members.

### Reference trees

- `./umbriel` — **primary** reference compositor (layouts, float, consume,
  foreign, scratchpads, animation tables, …). Prefer over peeble and over
  guessing niri.
- `./peeble` — secondary only for features Umbriel lacks (bar / launcher /
  services-shaped ideas). Do not port Go style into `./src`.
- niri — already digested by Umbriel; do not bypass Umbriel to “match niri.”
