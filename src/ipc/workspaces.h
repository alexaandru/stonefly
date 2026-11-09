#ifndef SF_IPC_WORKSPACES_H
#define SF_IPC_WORKSPACES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

struct sf_server;

struct sf_workspace_info {
	char output[64];
	int index; /* 1-based */
	char name[16];
	bool active;
	bool empty;
	int windows;
	char layout[16];
};

size_t sf_workspaces_collect(struct sf_server *server, struct sf_workspace_info *out, size_t cap);

int sf_workspaces_to_json(const struct sf_workspace_info *infos, size_t n, char *buf, size_t cap);

bool sf_workspaces_from_json(const char *json, struct sf_workspace_info *out, size_t cap,
			     size_t *n_out);

void sf_workspaces_print(const struct sf_workspace_info *infos, size_t n, FILE *fp);

#endif
