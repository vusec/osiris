#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hypermem.h"
#include "hypermem-api.h"

hypermem_entry_t hypermem_read(const struct hypermem_session *session) {
	hypermem_entry_t value;

	if (pread(session->mem_fd, &value, sizeof(value), session->address) <
		sizeof(value)) {
		fprintf(stderr, "memory read failed at 0x%lx: %s\n",
		        (long) session->address, strerror(errno));
		exit(-1);
	}
	return value;
}

void hypermem_write(const struct hypermem_session *session,
	hypermem_entry_t value) {
	if (pwrite(session->mem_fd, &value, sizeof(value), session->address) <
		sizeof(value)) {
		fprintf(stderr, "memory write failed at 0x%lx: %s\n",
		        (long) session->address, strerror(errno));
		exit(-1);
	}
}

int hypermem_connect(struct hypermem_session *session) {
	session->mem_fd = open("/dev/mem", O_RDWR);
	if (session->mem_fd < 0) return -1;

	session->address = HYPERMEM_BASEADDR;
	session->address = hypermem_read(session);
	if (session->address < HYPERMEM_BASEADDR ||
		session->address >= HYPERMEM_BASEADDR + HYPERMEM_SIZE) {
		if (close(session->mem_fd) < 0) {
			perror("close failed");
			exit(-1);
		}
		errno = EHOSTUNREACH;
		return -1;
	}

	hypermem_write(session, HYPERMEM_COMMAND_CONNECT);
	return 0;
}

void hypermem_disconnect(struct hypermem_session *session) {
	hypermem_write(session, HYPERMEM_COMMAND_DISCONNECT);
	if (close(session->mem_fd) < 0) {
		perror("close failed");
		exit(-1);
	}
}

#endif
