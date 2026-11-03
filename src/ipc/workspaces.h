#ifndef STONEFLY_IPC_WORKSPACES_H
#define STONEFLY_IPC_WORKSPACES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

struct stonefly_server;

struct stonefly_workspace_info {
	char output[64];
	int index; /* 1-based */
	char name[16];
	bool active;
	bool empty;
	int windows;
	char layout[16];
};

size_t stonefly_workspaces_collect(struct stonefly_server *server,
				   struct stonefly_workspace_info *out, size_t cap);

int stonefly_workspaces_to_json(const struct stonefly_workspace_info *infos, size_t n, char *buf,
				size_t cap);

bool stonefly_workspaces_from_json(const char *json, struct stonefly_workspace_info *out,
				   size_t cap, size_t *n_out);

void stonefly_workspaces_print(const struct stonefly_workspace_info *infos, size_t n, FILE *fp);

#endif
