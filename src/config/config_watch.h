/* inotify watch on the user config directory (Umbriel ConfigWatcher shape). */
#ifndef SF_CONFIG_WATCH_H
#define SF_CONFIG_WATCH_H

struct sf_server;

/* Arm or disarm from sf.opt.config_watch + event loop. Safe to call repeatedly. */
void sf_config_watch_sync(struct sf_server *server);

/* Drop fd/timer sources (server finish). */
void sf_config_watch_finish(struct sf_server *server);

#endif /* SF_CONFIG_WATCH_H */
