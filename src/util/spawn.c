#include "util/spawn.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "log/log.h"

static void reap_children(int signo) {
	(void)signo;
	int saved = errno;
	while (waitpid(-1, NULL, WNOHANG) > 0) {
	}
	errno = saved;
}

void sf_spawn_install_reaper(void) {
	struct sigaction sa = {0};
	sa.sa_handler = reap_children;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
	sigaction(SIGCHLD, &sa, NULL);
}

/* Between fork and exec: async-signal-safe only. */
static void spawn_child_prepare(void) {
	struct sigaction sa = {0};
	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGCHLD, &sa, NULL);
	sigset_t empty;
	sigemptyset(&empty);
	sigprocmask(SIG_SETMASK, &empty, NULL);
	setsid();
}

void sf_spawn_cmd(const char *cmd) {
	if (cmd == NULL || cmd[0] == '\0') {
		return;
	}
	pid_t pid = fork();
	if (pid < 0) {
		SF_LOGW("spawn fork failed", sf_str("cmd", cmd), sf_str("err", strerror(errno)));
		return;
	}
	if (pid == 0) {
		spawn_child_prepare();
		execl("/bin/sh", "/bin/sh", "-c", cmd, (void *)NULL);
		_exit(1);
	}
}
