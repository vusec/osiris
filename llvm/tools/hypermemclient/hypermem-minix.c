#ifdef __minix

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "hypermem.h"

static volatile hypermem_entry_t *get_hypermem_vaddr(void) {
	char buf[12], *bufend;
	ssize_t bufsz;
	int fd;
	long hypermem_vaddr;

	/* Determine virtual address where hypermem communication area is mapped */
	fd = open("/proc/hypermem", O_RDONLY);
	if (fd < 0) {
		perror("error: /proc/hypermem not available");
		return NULL;
	}

	bufsz = read(fd, buf, sizeof(buf));
	if (bufsz < 0) {
		perror("error: /proc/hypermem read failed");
		return NULL;
	}
	if (bufsz < 1 || bufsz >= sizeof(buf)) {
		fprintf(stderr, "error: /proc/hypermem value has invalid length: %d\n", (int) bufsz);
		errno = E2BIG;
		return NULL;
	}
	buf[bufsz] = 0;

	hypermem_vaddr = strtoul(buf, &bufend, 0);
	while (*bufend && isspace((int) *bufend)) bufend++;
	if (bufend != buf + bufsz) {
		fprintf(stderr, "error: /proc/hypermem value invalid: %s\n", buf);
		errno = EINVAL;
		return NULL;
	}

	close(fd);

	assert(sizeof(hypermem_vaddr) == sizeof(void *));
	return (volatile hypermem_entry_t *) hypermem_vaddr;
}

hypermem_entry_t hypermem_read(const struct hypermem_session *session) {
	assert(session);
	return *session->address;
}

void hypermem_write(const struct hypermem_session *session,
                           hypermem_entry_t value) {

	assert(session);
	*session->address = value;
}

int hypermem_connect(struct hypermem_session *session) {
	hypermem_entry_t channel, channelidx;
	volatile hypermem_entry_t *hypermem_vaddr;

	/* The kernel will remap the hypermem memory interval for us at hypermem_vaddr */
	hypermem_vaddr = get_hypermem_vaddr();
	if (!hypermem_vaddr) return -1;
	session->address = session->mem_base = hypermem_vaddr;

	channel = hypermem_read(session);
	if (channel < HYPERMEM_BASEADDR ||
		channel >= (HYPERMEM_BASEADDR + HYPERMEM_SIZE) ||
		channel % sizeof(hypermem_entry_t) != 0) {
		fprintf(stderr, "error: hypermem connect failed. Channel %lx out of range\n", (long) channel);
		errno = EHOSTUNREACH;
		return -1;
	}
	channelidx = (channel - HYPERMEM_BASEADDR) / sizeof(hypermem_entry_t);
	session->address = session->mem_base + channelidx;
	hypermem_write(session, HYPERMEM_COMMAND_CONNECT);
	return 0;
}

void hypermem_disconnect(struct hypermem_session *session) {
	hypermem_write(session, HYPERMEM_COMMAND_DISCONNECT);
}

#endif
