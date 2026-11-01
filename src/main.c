#define _POSIX_C_SOURCE 200809L

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "ipc/ipc.h"
#include "log/log.h"
#include "server/server.h"

static void usage(const char *argv0) {
	printf("Usage:\n");
	printf("  %s [-s startup command]   run the compositor\n", argv0);
	printf("  %s windows [--json]       list windows via IPC\n", argv0);
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
	if (argc >= 2 && argv[1][0] != '-') {
		if (strcmp(argv[1], "windows") == 0) {
			return stonefly_ipc_cmd_windows(argc - 1, argv + 1);
		}
		fprintf(stderr, "stonefly: unknown command %s\n", argv[1]);
		usage(argv[0]);
		return 1;
	}
	return run_compositor(argc, argv);
}
