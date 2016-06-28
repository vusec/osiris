#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hypermem.h"

int main(int argc, char **argv) {
	char **arg;
	const char *cmd, *str, *str2;
	int index;
	int r = 0;
	struct hypermem_session session;

	if (hypermem_connect(&session) < 0) {
		perror("error: cannot connect to hypervisor");
		return 1;
	}

	arg = argv + 1;
	while (*arg) {
		/* note: edfi_context_set, edfi_faultindex_get and fault calls
		 * not available from user space; they require memory access
		 * to this process, not /dev/mem context
		 */
		cmd = *(arg++);
		if (strcmp(cmd, "dump") == 0) {
			str = *arg ? *(arg++) : "hypermemclient";
			hypermem_edfi_dump_stats(&session, str);
		} else if (strcmp(cmd, "dumpmod") == 0) {
			str = *arg ? *(arg++) : "rs";
			str2 = *arg ? *(arg++) : "hypermemclient";
			hypermem_edfi_dump_stats_module(&session, str, str2);
		} else if (strcmp(cmd, "fault") == 0) {
			str = *arg ? *(arg++) : "rs";
			index = *arg ? atoi(*(arg++)) : 0;
			hypermem_fault(&session, str, index);
		} else if (strcmp(cmd, "faultindex") == 0) {
			str = *arg ? *(arg++) : "rs";
			index = hypermem_edfi_faultindex_get(&session, str);
			printf("%d\n", index);
		} else if (strcmp(cmd, "magic") == 0) {
			hypermem_magic_register(&session);
		} else if (strcmp(cmd, "nop") == 0) {
			if (hypermem_nop(&session)) {
				printf("NOP return value correct\n");
			} else {
				printf("NOP return value incorrect\n");
				r = 1;
			}
		} else if (strcmp(cmd, "print") == 0) {
			str = *arg ? *(arg++) : "Checkpoint";
			hypermem_print(&session, str);
		} else if (strcmp(cmd, "quit") == 0) {
			hypermem_quit(&session);
		} else if (strcmp(cmd, "releasecr3") == 0) {
			index = *arg ? atoi(*(arg++)) : 0;
			hypermem_release_cr3(&session, index);
		} else if (strcmp(cmd, "setcr3") == 0) {
			index = *arg ? atoi(*(arg++)) : 0;
			hypermem_set_cr3(&session, index);
		} else if (strcmp(cmd, "st") == 0) {
			hypermem_magic_st(&session);
		} else {
			fprintf(stderr, "error: invalid command \"%s\"\n", cmd);
			fprintf(stderr, "available commands are: dump, dumpmod, fault, faultindex, magic, nop, print,\n");
			fprintf(stderr, "                        releasecr3, setcr3, st\n");
			r = 2;
			break;
		}
	}
	hypermem_disconnect(&session);
	return r;
}
