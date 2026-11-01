#ifndef SF_BACKGROUND_EFFECT_H
#define SF_BACKGROUND_EFFECT_H

struct sf_server;

/* Advertise ext-background-effect-v1 (blur capability). Non-empty client blur
 * region sets toplevel->client_blur or layer client blur; SceneFX full-surface
 * plate (region mask still open). */
void sf_background_effect_setup(struct sf_server *server);
void sf_background_effect_finish(struct sf_server *server);

#endif
