#ifndef STONEFLY_BACKGROUND_EFFECT_H
#define STONEFLY_BACKGROUND_EFFECT_H

struct stonefly_server;

/* Advertise ext-background-effect-v1 (blur capability). Non-empty client blur
 * region sets toplevel->client_blur; SceneFX still does full-window blur. */
void stonefly_background_effect_setup(struct stonefly_server *server);
void stonefly_background_effect_finish(struct stonefly_server *server);

#endif
