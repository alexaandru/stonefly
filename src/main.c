#define _POSIX_C_SOURCE 200809L

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "config/config.h"
#include "ipc/ipc.h"
#include "log/log.h"
#include "server/server.h"

static void usage(const char *argv0) {
	printf("Usage:\n");
	printf("  %s [-s startup command]         run the compositor\n", argv0);
	printf("  %s --write-config               write default Fennel-first config tree\n", argv0);
	printf("  %s windows [--json]             list windows via IPC\n", argv0);
	printf("  %s layers [--json]              list layer-shell surfaces via IPC\n", argv0);
	printf("  %s outputs [--json]             list outputs via IPC\n", argv0);
	printf("  %s config [section] [--json]    dump live config (or one section)\n", argv0);
	printf("  %s focus <id>                   focus window by id\n", argv0);
	printf("  %s close <id>                   close window by id\n", argv0);
	printf("  %s toggle-quick-terminal        toggle scratchpad pad \"term\"\n", argv0);
}

static int run_compositor(int argc, char *argv[]) {
	char *startup_cmd = NULL;
	int c;
	optind = 1;
	while ((c = getopt(argc, argv, "s:h")) != -1) {
		switch (c) {
		case 's':
			startup_cmd = optarg;
			break;
		default:
			usage(argv[0]);
			return c == 'h' ? 0 : 1;
		}
	}
	if (optind < argc) {
		usage(argv[0]);
		return 1;
	}

	sf_log_init(SF_LOG_DEBUG);
	signal(SIGCHLD, SIG_IGN);

	struct stonefly_server server = {0};
	if (!stonefly_server_init(&server)) {
		sf_log_finish();
		return 1;
	}
	int rc = stonefly_server_run(&server, startup_cmd);
	stonefly_server_finish(&server);
	sf_log_finish();
	return rc;
}

int main(int argc, char *argv[]) {
	if (argc >= 2 && strcmp(argv[1], "--write-config") == 0) {
		return stonefly_config_write_default();
	}
	if (argc >= 2 && argv[1][0] != '-') {
		if (strcmp(argv[1], "windows") == 0) {
			return stonefly_ipc_cmd_windows(argc - 1, argv + 1);
		}
		if (strcmp(argv[1], "layers") == 0) {
			return stonefly_ipc_cmd_layers(argc - 1, argv + 1);
		}
		if (strcmp(argv[1], "outputs") == 0) {
			return stonefly_ipc_cmd_outputs(argc - 1, argv + 1);
		}
		if (strcmp(argv[1], "config") == 0) {
			return stonefly_ipc_cmd_config(argc - 1, argv + 1);
		}
		if (strcmp(argv[1], "focus") == 0) {
			return stonefly_ipc_cmd_focus(argc - 1, argv + 1);
		}
		if (strcmp(argv[1], "close") == 0) {
			return stonefly_ipc_cmd_close(argc - 1, argv + 1);
		}
		if (strcmp(argv[1], "toggle-quick-terminal") == 0) {
			return stonefly_ipc_cmd_toggle_quick_terminal(argc - 1, argv + 1);
		}
		fprintf(stderr, "stonefly: unknown command %s\n", argv[1]);
		usage(argv[0]);
		return 1;
	}
	return run_compositor(argc, argv);
}
