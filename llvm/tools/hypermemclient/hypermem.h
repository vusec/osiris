#ifndef HYPERMEM_H
#define HYPERMEM_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "hypermem-api.h"

#if defined(__linux__)
struct hypermem_session {
	int mem_fd;
	off_t address;
};
#elif defined(__minix)
struct hypermem_session {
	volatile hypermem_entry_t *address;
	volatile hypermem_entry_t *mem_base;
};
#else
#error Platform not supported
struct hypermem_session;
#endif

/* hypermem-platform.c */
int hypermem_connect(struct hypermem_session *session);
void hypermem_disconnect(struct hypermem_session *session);
hypermem_entry_t hypermem_read(const struct hypermem_session *session);
void hypermem_write(const struct hypermem_session *session, hypermem_entry_t value);

/* hypermem.c */
void hypermem_edfi_context_set(const struct hypermem_session *session,
	const char *name, const void *context, ptrdiff_t ptroffset);
void hypermem_edfi_dump_stats(const struct hypermem_session *session,
	const char *message);
void hypermem_edfi_dump_stats_module(const struct hypermem_session *session,
	const char *name, const char *message);
int hypermem_edfi_faultindex_get(const struct hypermem_session *session,
	const char *name);
void hypermem_fault(const struct hypermem_session *session, const char *name,
	unsigned bbindex);
void hypermem_magic_register(const struct hypermem_session *session);
void hypermem_magic_st_module(const struct hypermem_session *session);
void hypermem_magic_st(const struct hypermem_session *session);
int hypermem_nop(const struct hypermem_session *session);
void hypermem_print(const struct hypermem_session *session, const char *str);
void hypermem_quit(const struct hypermem_session *session);
void hypermem_release_cr3(const struct hypermem_session *session,
    uint32_t cr3);
void hypermem_set_cr3(const struct hypermem_session *session, uint32_t cr3);

#endif /* !defined(HYPERMEM_H) */
