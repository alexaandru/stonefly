#include "ui/ui.h"

#include "config/config.h"
#include "server/server.h"
#include "ui/text.h"

void sf_ui_apply_config(struct sf_server *server) {
	if (server == NULL) {
		return;
	}
	const struct sf_config *cfg = sf_cfg(server);
	(void)sf_ui_text_init(cfg->cheatsheet_font_size, cfg->cheatsheet_font);
}

void sf_ui_init(struct sf_server *server) {
	sf_ui_apply_config(server);
}

void sf_ui_fini(struct sf_server *server) {
	sf_cheatsheet_hide(server);
	sf_ui_text_fini();
}
