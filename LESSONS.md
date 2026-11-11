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

### Minimize is float-only (Umbriel)

Tiled windows do not minimize into a hole under the strip. A hidden tile must
not keep a ghost packing slot. Stonefly still allows tile minimize but skips
minimized windows in packing; foreign must keep advertising them.

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
| Lone column fills usable | yes; 2+ use per-column `width_frac` |
| Insert after focused column | yes (first-class strip index) |
| First-class Column objects | `sf_column` + `ws->columns` |
| Per-row height weights | `column_row_weight` + end gap weights |
| Consume remembers extent | `column_extent_saved` → expel restore |
| Foreign pin to workspace output | fixed (was geom∩ → taskbar flicker) |
| Drag into column / new column | Super+LMB: cell mid = swap; seam = insert; gap = new col |
| Drag preview matches op | full cell / seam strip / gap strip |
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
