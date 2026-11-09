#ifndef STONEFLY_RULES_H
#define STONEFLY_RULES_H

#include <stdbool.h>

struct stonefly_toplevel;
struct stonefly_window_rule;
struct stonefly_server;

/* Longest-matching window_rule for this toplevel's app_id/title, or NULL. */
const struct stonefly_window_rule *stonefly_rules_match(const struct stonefly_toplevel *toplevel);

/* Apply matched rule actions (float/size/ssd/borderless). Placement is separate. */
void stonefly_rules_apply(struct stonefly_toplevel *toplevel);

/* True when the best match asks to center. */
bool stonefly_rules_wants_center(const struct stonefly_toplevel *toplevel);

/* Late title/app_id: re-engage auto_place when a center rule now matches. */
void stonefly_rules_consider_place(struct stonefly_toplevel *toplevel);

/* Re-apply rules to every mapped toplevel (config reload). */
void stonefly_rules_reapply_all(struct stonefly_server *server);

#endif /* STONEFLY_RULES_H */
