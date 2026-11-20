/* sf-suite cover=ipc/send.h */
#include "sf_test.h"
#include "ipc/send.h"

#include <string.h>
#include <unistd.h>

static void test_backpressure(void) {
	int pair[2];
	SF_TEST_EQ_INT(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
	int small = 4096;
	SF_TEST_EQ_INT(setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof(small)), 0);
	char msg[65536];
	for (size_t i = 0; i < sizeof(msg); i++) {
		msg[i] = (char)(i % 127);
	}
	size_t sent = 0, received = 0;
	SF_TEST_TRUE(sf_ipc_send_pending(pair[0], msg, sizeof(msg), &sent));
	SF_TEST_TRUE(sent > 0 && sent < sizeof(msg));
	size_t blocked = sent;
	SF_TEST_TRUE(sf_ipc_send_pending(pair[0], msg, sizeof(msg), &sent));
	SF_TEST_TRUE(sent == blocked);
	while (received < sizeof(msg)) {
		char chunk[2048];
		ssize_t n = recv(pair[1], chunk, sizeof(chunk), MSG_DONTWAIT);
		SF_TEST_TRUE(n > 0);
		SF_TEST_TRUE(received + (size_t)n <= sizeof(msg));
		SF_TEST_EQ_INT(memcmp(chunk, msg + received, (size_t)n), 0);
		received += (size_t)n;
		SF_TEST_TRUE(sf_ipc_send_pending(pair[0], msg, sizeof(msg), &sent));
	}
	close(pair[1]);
	sent = 0;
	SF_TEST_TRUE(!sf_ipc_send_pending(pair[0], msg, sizeof(msg), &sent));
	close(pair[0]);
}

static const struct sf_test tests[] = {{"sf_ipc_send_pending", test_backpressure}};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
