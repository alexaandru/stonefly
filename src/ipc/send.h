#ifndef SF_IPC_SEND_H
#define SF_IPC_SEND_H

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/socket.h>

/* Nonblocking socket. A short successful return leaves the suffix for POLLOUT. */
static inline bool sf_ipc_send_pending(int fd, const char *buf, size_t len, size_t *offset) {
	while (*offset < len) {
		ssize_t n = send(fd, buf + *offset, len - *offset, MSG_NOSIGNAL | MSG_DONTWAIT);
		if (n > 0) {
			*offset += (size_t)n;
		} else if (n < 0 && errno == EINTR) {
			continue;
		} else {
			return n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
		}
	}
	return true;
}

#endif
