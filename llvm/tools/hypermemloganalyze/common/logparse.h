#ifndef LOGPARSE_H
#define LOGPARSE_H

#include <stdint.h>
#include <sys/time.h>

#include "common.h"

struct logparse_callback_state {
	struct timeval timestamp;
	const char *logpath;
	int lineno;
	void *arg;
};

enum minix_test_status {
	mts_running,
	mts_passed,
	mts_failed,
};

enum ltckpt_closereason {
	lcr_unknown,
	lcr_ipc,
	lcr_writelog_full,
	ltckpt_closereason_count
};

enum ltckpt_method {
	lm_generic,
	lm_deadlock,
	lm_failstop,
	lm_idempotent,
	lm_process_specific,
	lm_service_fi,
	lm_writelog,
	ltckpt_method_count
};

enum ltckpt_status {
	ls_start,
	ls_failure,
	ls_shutdown,
	ls_success,
	ltckpt_status_count
};

struct logparse_callbacks {
	void *arg;

	/* output directly from QEMU */
	void (* edfi_context_release)(
		const struct logparse_callback_state *state,
		const char *name, size_t namesz);
	void (* edfi_context_reset)(
		const struct logparse_callback_state *state,
		const char *name, size_t namesz);
	void (* edfi_context_set)(
		const struct logparse_callback_state *state,
		const char *name, size_t namesz);
	void (* edfi_dump_stats)(
		const struct logparse_callback_state *state,
		const char *msg, size_t msgsz);
	void (* edfi_dump_stats_module)(
		const struct logparse_callback_state *state,
		const char *name, size_t namesz,
		const char *msg, size_t msgsz,
		execcount *bbs,	size_t bb_count);
	void (* edfi_faultindex_get)(
		const struct logparse_callback_state *state,
		const char *name, size_t namesz,
		long bbindex);
	void (* fault)(
		const struct logparse_callback_state *state,
		const char *name, size_t namesz,
		long bbindex,
		long repeat);
	void (* hypermem_error)(
		const struct logparse_callback_state *state,
		const char *desc, size_t descsz);
	void (* hypermem_faultspec)(
		const struct logparse_callback_state *state,
		const char *faultspec, size_t faultspecsz);
	void (* hypermem_flushlog)(
		const struct logparse_callback_state *state,
		int enable);
	void (* hypermem_logpath)(
		const struct logparse_callback_state *state,
		const char *logpath, size_t logpathsz);
	void (* hypermem_warning)(
		const struct logparse_callback_state *state,
		const char *desc, size_t descsz);
	void (* nop)(const struct logparse_callback_state *state);
	void (* qemu_drive)(
		const struct logparse_callback_state *state,
		long index,
		const char *img, size_t imgsz);
	void (* qemu_exit)(const struct logparse_callback_state *state);
	void (* qemu_hypermem_reset)(const struct logparse_callback_state *state);
	void (* qemu_interrupt)(
		const struct logparse_callback_state *state,
		long number);
	void (* qemu_powerdown)(const struct logparse_callback_state *state);
	void (* qemu_reset)(const struct logparse_callback_state *state);
	void (* qemu_shutdown)(const struct logparse_callback_state *state);
	void (* qemu_signal)(
		const struct logparse_callback_state *state,
		long number);
	void (* quit)(const struct logparse_callback_state *state);

	/* output from MINIX (SEF) */
	void (* print_debug)(
		const struct logparse_callback_state *state,
		const char *msg, size_t msgsz);
	void (* print_rs_crash)(
		const struct logparse_callback_state *state,
		const char *name, size_t namesz,
		long signal);
	void (* print_rs_normal)(
		const struct logparse_callback_state *state,
		const char *name, size_t namesz);
	void (* print_startup)(
		const struct logparse_callback_state *state,
		const char *name, size_t namesz);

	/* output from llvm/tools/edfi/qemu_scripts */
	void (* print_end)(const struct logparse_callback_state *state);
	void (* print_sanity)(
		const struct logparse_callback_state *state,
		int passed, int posttest);
	void (* print_start)(const struct logparse_callback_state *state);
	void (* print_workload)(
		const struct logparse_callback_state *state,
		const char *name, size_t namesz);
	void (* print_workload_completed)(const struct logparse_callback_state *state);

	/* output from prun-scripts/ltckpt-edfi-boot.sh */
	void (* print_abort_after_restart)(const struct logparse_callback_state *state);

	/* output from MINIX test set */
	void (* print_test)(
		const struct logparse_callback_state *state,
		const char *name, size_t namesz,
		enum minix_test_status status);
	void (* print_test_output)(
		const struct logparse_callback_state *state,
		const char *name, size_t namesz,
		const char *msg, size_t msgsz);
	void (* print_tests_completed)(
		const struct logparse_callback_state *state,
		const char *namesbad, size_t namessz);
	void (* print_tests_starting)(
		const struct logparse_callback_state *state,
		const char *names, size_t namessz);

	/* output from LTCKPT */
	void (* print_ltckpt)(
		const struct logparse_callback_state *state,
		enum ltckpt_status status,
		enum ltckpt_method method,
		const char *name, size_t namesz);
	void (* print_ltckpt_closed)(
		const struct logparse_callback_state *state,
		enum ltckpt_closereason reason,
		const char *name, size_t namesz);
	void (* print_ltckpt_closed_ipc_callsites)(
		const struct logparse_callback_state *state,
		const char *name, size_t namesz,
		long totaltimes,
		long totalsites,
		const uint64_t *callsites,
		size_t callsitecount);
};

void logparse_from_path(
	const char *logpath,
	const struct logparse_callbacks *callbacks,
	struct timeval *timestamp_last);

#endif
