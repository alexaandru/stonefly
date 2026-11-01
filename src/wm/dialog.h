/* Modal dialog attach (xdg_wm_dialog_v1) — Umbriel-shaped subset.
 * Modal + parent → sticky float, scene child of parent, parent shade,
 * focus redirect. */
#ifndef SF_WM_DIALOG_H
#define SF_WM_DIALOG_H

#include <stdbool.h>

struct sf_toplevel;

/* True when this window is scene-attached under a modal parent. */
bool sf_toplevel_modal_attached(const struct sf_toplevel *toplevel);

/* Deepest modal child hanging on toplevel (Umbriel inputDelegate). */
struct sf_toplevel *sf_toplevel_modal_input_delegate(struct sf_toplevel *toplevel);

/* Apply / clear attach from modal flag + parent. */
void sf_toplevel_modal_reconcile(struct sf_toplevel *toplevel);

/* Parent moved/resized: re-center attached dialogs + shade size. */
void sf_toplevel_modal_layout_attachments(struct sf_toplevel *parent);

/* Parent or dialog teardown. */
void sf_toplevel_modal_release_attachments(struct sf_toplevel *parent);
void sf_toplevel_modal_detach(struct sf_toplevel *toplevel);

#endif
