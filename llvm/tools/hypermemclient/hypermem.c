#include <string.h>

#include "hypermem.h"
#include "hypermem-api.h"

static void hypermem_write_string(const struct hypermem_session *session,
	const char *str) {
	hypermem_entry_t buf;
	size_t chunk, len = strlen(str);

	hypermem_write(session, len);
	while (len > 0) {
		chunk = sizeof(hypermem_entry_t);
		if (chunk > len) chunk = len;
		buf = 0;
		memcpy(&buf, str, chunk);
		hypermem_write(session, buf);
		str += chunk;
		len -= chunk;
	}
}

void hypermem_edfi_context_set(const struct hypermem_session *session,
	const char *name, const void *context, ptrdiff_t ptroffset) {
	hypermem_write(session, HYPERMEM_COMMAND_EDFI_CONTEXT_SET);
	hypermem_write(session, (hypermem_entry_t) context);
	hypermem_write(session, (hypermem_entry_t) ptroffset);
	hypermem_write_string(session, name);
}

void hypermem_edfi_dump_stats(const struct hypermem_session *session,
	const char *message) {
	hypermem_write(session, HYPERMEM_COMMAND_EDFI_DUMP_STATS);
	hypermem_write_string(session, message);
}

void hypermem_edfi_dump_stats_module(const struct hypermem_session *session,
	const char *name, const char *message) {
	hypermem_write(session, HYPERMEM_COMMAND_EDFI_DUMP_STATS_MODULE);
	hypermem_write_string(session, name);
	hypermem_write_string(session, message);
}

int hypermem_edfi_faultindex_get(const struct hypermem_session *session,
	const char *name) {
	hypermem_write(session, HYPERMEM_COMMAND_EDFI_FAULTINDEX_GET);
	hypermem_write_string(session, name);
	return hypermem_read(session);
}

void hypermem_fault(const struct hypermem_session *session, const char *name,
	unsigned bbindex) {
	hypermem_write(session, HYPERMEM_COMMAND_FAULT);
	hypermem_write(session, bbindex);
	hypermem_write_string(session, name);
}

void hypermem_magic_register(const struct hypermem_session *session) {
	const char *module = "test_module";
	hypermem_write(session, HYPERMEM_COMMAND_MAGIC_CONTEXT_SET);
	hypermem_write(session, strlen(module));
	hypermem_write(session, (hypermem_entry_t) (long) module);
	hypermem_write(session, (hypermem_entry_t) 0xdeadbeef);
	hypermem_write(session, (hypermem_entry_t) 0x1337);
}

void hypermem_magic_st_module(const struct hypermem_session *session) {
	const char *module = "test_module";
	hypermem_write(session, HYPERMEM_COMMAND_MAGIC_ST);
	hypermem_write(session, strlen(module));
	hypermem_write(session, (hypermem_entry_t) (long) module);
}

void hypermem_magic_st(const struct hypermem_session *session) {
	hypermem_write(session, HYPERMEM_COMMAND_MAGIC_ST_ALL);
}

int hypermem_nop(const struct hypermem_session *session) {
	hypermem_write(session, HYPERMEM_COMMAND_NOP);
	return hypermem_read(session) == HYPERCALL_NOP_REPLY;
}

void hypermem_print(const struct hypermem_session *session, const char *str) {
	hypermem_write(session, HYPERMEM_COMMAND_PRINT);
	hypermem_write_string(session, str);
}

void hypermem_quit(const struct hypermem_session *session) {
	hypermem_write(session, HYPERMEM_COMMAND_QUIT);
}

void hypermem_release_cr3(const struct hypermem_session *session,
                          uint32_t cr3) {
    hypermem_write(session, HYPERMEM_COMMAND_RELEASE_CR3);
    hypermem_write(session, (hypermem_entry_t)cr3);
}

void hypermem_set_cr3(const struct hypermem_session *session, uint32_t cr3) {
    hypermem_write(session, HYPERMEM_COMMAND_SET_CR3);
    hypermem_write(session, (hypermem_entry_t)cr3);
}
