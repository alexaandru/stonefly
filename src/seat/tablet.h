#ifndef SF_SEAT_TABLET_H
#define SF_SEAT_TABLET_H

struct sf_server;

/* Advertise zwp_tablet_v2 and wire tools/pads (pressure, tilt, ExpressKeys). */
void sf_tablet_setup(struct sf_server *server);
void sf_tablet_finish(struct sf_server *server);

#endif
