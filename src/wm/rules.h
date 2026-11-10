#ifndef SF_RULES_H
#define SF_RULES_H

#include <stdbool.h>

struct sf_toplevel;
struct sf_window_rule;
struct sf_server;

/* Longest-matching window_rule for this toplevel's app_id/title, or NULL. */
const struct sf_window_rule *sf_rules_match(const struct sf_toplevel *toplevel);

/* Apply matched rule actions (float/size/ssd/borderless). Placement is separate. */
void sf_rules_apply(struct sf_toplevel *toplevel);

/* True when the best match asks to center. */
bool sf_rules_wants_center(const struct sf_toplevel *toplevel);

/* True when some center window_rule is not yet ruled out by known title/app_id
 * (missing fields could still make it match). SoftPlace appear-hold uses this. */
bool sf_rules_center_still_possible(const struct sf_toplevel *toplevel);

/* Late title/app_id: re-engage managed place when a center rule now matches. */
void sf_rules_consider_place(struct sf_toplevel *toplevel);

/* Re-apply rules to every mapped toplevel (config reload). */
void sf_rules_reapply_all(struct sf_server *server);

#endif /* SF_RULES_H */
