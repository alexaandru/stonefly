#include "ui/ui.h"

#include "ui/text.h"

void sf_ui_init(struct sf_server *server) {
	(void)server;
	(void)sf_ui_text_init(12);
}

void sf_ui_fini(struct sf_server *server) {
	sf_cheatsheet_hide(server);
	sf_ui_text_fini();
}
