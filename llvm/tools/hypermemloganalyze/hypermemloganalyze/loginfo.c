#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "debug.h"
#include "helper.h"
#include "loginfo.h"
#include "logparse.h"

void loginfo_free(struct loginfo *loginfo) {
	struct loginfo_module *module, *module_next;

	if (!loginfo) return;
	FREE(loginfo->drive0);
	FREE(loginfo->drive1);
	FREE(loginfo->faultspec);
	FREE(loginfo->logpath);
	FREE(loginfo->system);
	FREE(loginfo->workload_reported);
	
	module = loginfo->modules;
	while (module) {
		FREE(module->name);
#ifndef IGNORE_EXECCOUNTS
		FREE(module->execcount);
#endif

		module_next = module->next;
		FREE(module);
		module = module_next;
	}
}

static struct loginfo_module *loginfo_module_add(struct loginfo *loginfo,
	const char *name, size_t namelen) {
	struct loginfo_module *module, **module_p;

	assert(loginfo);
	assert(name);

	module_p = &loginfo->modules;
	while ((module = *module_p)) {
		if (strlen(module->name) == namelen &&
			strncmp(module->name, name, namelen) == 0) {
			return module;
		}
		module_p = &module->next;
	}

	*module_p = module = CALLOC(1, struct loginfo_module);
	module->name = STRNDUP(name, namelen);
	return module;
}

static void set_logstep(const struct logparse_callback_state *state,
	struct loginfo *loginfo, enum logstep step) {

	if (loginfo->step >= step) {
		fprintf(stderr, "warning: unexpected step transition %s -> %s "
			"on line %d of log file %s\n",
			logstepname(loginfo->step), logstepname(step),
			state->lineno, state->logpath);
	} else {
		loginfo->step_time[step] = state->timestamp;
	}
	loginfo->step = step;
}

static void loginfo_line_handler_qemu_drive(
	const struct logparse_callback_state *state,
	long index,
	const char *img, size_t imgsz) {
	struct loginfo *loginfo = state->arg;
	char **drive_p;

	if (index == 0) {
		drive_p = &loginfo->drive0;
	} else if (index == 1) {
		drive_p = &loginfo->drive1;
	} else {
		fprintf(stderr, "warning: unexpected drive index %ld at "
			"line %d of log file %s\n",
			index, state->lineno, state->logpath);
		return;
	}

	if (*drive_p) {
		fprintf(stderr, "warning: duplicate drive specification at "
			"line %d of log file %s\n",
			state->lineno, loginfo->logpath);
		return;
	}

	*drive_p = STRNDUP(img, imgsz);
}

static void loginfo_line_handler_edfi_dump_stats_module(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz,
	const char *msg, size_t msgsz,
	execcount *bbs,	size_t bb_count) {
	struct loginfo *loginfo = state->arg;
	struct loginfo_module *module;

	/* locate/add the module by name */
	module = loginfo_module_add(loginfo, name, namesz);
	dbgprintf_v("loading execution counts for module %s\n", module->name);

	module->stats[loginfo->step].time = state->timestamp;

	/* read execution counts */
#ifndef IGNORE_EXECCOUNTS
	if (!module->bb_count) {
		module->bb_count = bb_count;
	} else if (module->bb_count != bb_count) {
		fprintf(stderr, "warning: inconsistent basic block counts "
			"%ld and %ld for module %s (latter is from line %d "
			"of log file %s)\n", (long) module->bb_count,
			(long) bb_count, module->name, state->lineno,
			state->logpath);
		return;
	}

	/* replace stored execution counts, retaining only the last */
	if (!module->execcount) module->execcount = MALLOC(bb_count, execcount);
	memcpy(module->execcount, bbs, bb_count * sizeof(execcount));
#endif
}

static void loginfo_line_handler_edfi_context_reset(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz) {
	struct loginfo *loginfo = state->arg;
	struct loginfo_module *module;

	module = loginfo_module_add(loginfo, name, namesz);
	module->restart_count[loginfo->step]++;
	timeval_set_to_min(&module->restart_time_first[loginfo->step],
		&state->timestamp);
	timeval_set_to_max(&module->restart_time_last[loginfo->step],
		&state->timestamp);
}

static void loginfo_line_handler_edfi_context_set(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz) {
	struct loginfo *loginfo = state->arg;
	struct loginfo_module *module;

	module = loginfo_module_add(loginfo, name, namesz);
	module->time_context_set = state->timestamp;
}

static void loginfo_line_handler_edfi_dump_stats(
	const struct logparse_callback_state *state,
	const char *msg, size_t msgsz) {
	struct loginfo *loginfo = state->arg;

	if (loginfo->step == logstep_qemu_start) {
		set_logstep(state, loginfo, logstep_before_sanity_pre);
	}
}

static int module_name_matches(const char *modname, const char *fullname) {
	const char *fullname_end;
	size_t fullname_len;

	fullname_end = strchr(fullname, '@');
	fullname_len = fullname_end ? (fullname_end - fullname) :
		strlen(fullname);
	if (fullname_len != strlen(modname)) return 0;

	return strncmp(modname, fullname, fullname_len) == 0;
}

static void loginfo_line_handler_edfi_faultindex_get(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz,
	long bbindex) {
	struct loginfo *loginfo = state->arg;
	struct loginfo_module *module;
	char *nametmp;

	if (bbindex < 0) return;

	/* bbindex in log is one-based */
	nametmp = STRNDUP(name, namesz);
	for (module = loginfo->modules; module; module = module->next) {
		if (!module_name_matches(nametmp, module->name)) continue;

		if (!module->fault_bb_index_set) {
			module->fault_bb_index = bbindex;
			module->fault_bb_index_set = 1;
			module->time_faultindex_get = state->timestamp;
		} else if (module->fault_bb_index != bbindex) {
			fprintf(stderr, "warning: inconsistent fault "
				"specification on line %d of log file %s; "
				"module %s has bb index %ld, instance %s has "
				"bb index %d\n", state->lineno, state->logpath,
				nametmp, bbindex, module->name,
				module->fault_bb_index);
		}
	}
	FREE(nametmp);
}

static void loginfo_line_handler_print_end(
	const struct logparse_callback_state *state) {
	struct loginfo *loginfo = state->arg;

	set_logstep(state, loginfo, logstep_workload_end);
}

static void loginfo_line_handler_fault(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz,
	long bbindex,
	long repeat) {
	struct loginfo *loginfo = state->arg;
	struct loginfo_module *module;

	module = loginfo_module_add(loginfo, name, namesz);

	/* bbindex in log is one-based */
	if (!module->fault_bb_index_set) {
		fprintf(stderr, "warning: basic block %ld activated in "
			"module %s, but the faulty basic block index was never "
			"retrieved (on line %d of log file %s)\n", bbindex,
			module->name, state->lineno, state->logpath);
	} else if (module->fault_bb_index != bbindex) {
		fprintf(stderr, "warning: basic block %ld activated in "
			"module %s, expected basic block %d instead "
			"(on line %d of log file %s)\n", bbindex, module->name,
			module->fault_bb_index, state->lineno, state->logpath);
	}
	module->fault_count[loginfo->step] += repeat;
	timeval_set_to_min(&module->fault_time_first[loginfo->step],
		&state->timestamp);
	timeval_set_to_max(&module->fault_time_last[loginfo->step],
		&state->timestamp);
}

static void loginfo_line_handler_hypermem_faultspec(
	const struct logparse_callback_state *state,
	const char *faultspec, size_t faultspecsz) {
	struct loginfo *loginfo = state->arg;

	if (loginfo->faultspec) {
		fprintf(stderr, "warning: faultspec set a second time on "
			"line %d of log file %s\n",
			state->lineno, state->logpath);
		return;
	}
	loginfo->faultspec = STRNDUP(faultspec, faultspecsz);
}

static void loginfo_line_handler_hypermem_logpath(
	const struct logparse_callback_state *state,
	const char *logpath, size_t logpathsz) {
	struct loginfo *loginfo = state->arg;

	set_logstep(state, loginfo, logstep_qemu_start);
}

static void loginfo_line_handler_qemu_exit(
	const struct logparse_callback_state *state) {
	struct loginfo *loginfo = state->arg;

	set_logstep(state, loginfo, logstep_qemu_exit);
}

static void loginfo_line_handler_print_sanity(
	const struct logparse_callback_state *state,
	int passed,
	int posttest) {
	struct loginfo *loginfo = state->arg;

	set_logstep(state, loginfo,
		posttest ? logstep_after_sanity_post : logstep_after_sanity_pre);
	if (passed) {
		if (posttest) {
			loginfo->sanity_passed_post = 1;
		} else {
			loginfo->sanity_passed_pre = 1;
		}
	}
}

static void loginfo_line_handler_print_start(
	const struct logparse_callback_state *state) {
#ifdef LOGSTEP_ALL
	struct loginfo *loginfo = state->arg;

	set_logstep(state, loginfo, logstep_workload_start);
#endif
}

static void loginfo_line_handler_print_startup(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz) {
	struct loginfo *loginfo = state->arg;
	struct loginfo_module *module;

	module = loginfo_module_add(loginfo, name, namesz);
	module->time_startup = state->timestamp;
}

static void loginfo_line_handler_print_workload(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz) {
	struct loginfo *loginfo = state->arg;

	if (loginfo->workload_reported) {
		fprintf(stderr, "warning: workload set a second time on "
			"line %d of log file %s\n",
			state->lineno, state->logpath);
		return;
	}
	loginfo->workload_reported = STRNDUP(name, namesz);
#ifdef LOGSTEP_ALL
	set_logstep(state, loginfo, logstep_workload_report);
#endif
}

static void loginfo_line_handler_print_workload_completed(
	const struct logparse_callback_state *state) {
#ifdef LOGSTEP_ALL
	struct loginfo *loginfo = state->arg;

	set_logstep(state, loginfo, logstep_workload_completed);
#endif
}

static char *first_word(const char *s) {
	const char *word;

	while (*s && !isalnum((int) *s)) s++;
	word = s;
	while (*s && isalnum((int) *s)) s++;
	return STRNDUP(word, s - word);
}

void loginfo_load_from_log(const char *logpath, struct loginfo *loginfo) {
	struct logparse_callbacks callbacks;

	/* parse the file using logparse.c functions, with callbacks to here */
	memset(loginfo, 0, sizeof(*loginfo));
	loginfo->logpath = STRDUP(logpath);

	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.arg = loginfo;
	callbacks.edfi_context_reset = loginfo_line_handler_edfi_context_reset;
	callbacks.edfi_context_set = loginfo_line_handler_edfi_context_set;
	callbacks.edfi_dump_stats = loginfo_line_handler_edfi_dump_stats;
	callbacks.edfi_dump_stats_module = loginfo_line_handler_edfi_dump_stats_module;
	callbacks.edfi_faultindex_get = loginfo_line_handler_edfi_faultindex_get;
	callbacks.fault = loginfo_line_handler_fault;
	callbacks.hypermem_faultspec = loginfo_line_handler_hypermem_faultspec;
	callbacks.hypermem_logpath = loginfo_line_handler_hypermem_logpath;
	callbacks.print_end = loginfo_line_handler_print_end;
	callbacks.print_sanity = loginfo_line_handler_print_sanity;
	callbacks.print_start = loginfo_line_handler_print_start;
	callbacks.print_startup = loginfo_line_handler_print_startup;
	callbacks.print_workload = loginfo_line_handler_print_workload;
	callbacks.print_workload_completed = loginfo_line_handler_print_workload_completed;
	callbacks.qemu_drive = loginfo_line_handler_qemu_drive;
	callbacks.qemu_exit = loginfo_line_handler_qemu_exit;

	logparse_from_path(logpath, &callbacks, NULL);

	/* some derived fields */
	loginfo->system = first_word(basename_const(logpath));
	if (loginfo->drive1) {
		loginfo->workload = basename_const(loginfo->drive1);
	} else {
		fprintf(stderr, "warning: log file %s does not report "
			"workload\n", logpath);
	}
}

const char *logstepdesc(enum logstep step) {
	switch (step) {
	case logstep_before_qemu_start: return "qemustart";
	case logstep_qemu_start: return "boot";
	case logstep_before_sanity_pre: return "pre-test";
	case logstep_after_sanity_pre: return "workload";
#ifdef LOGSTEP_ALL
	case logstep_workload_start: return "workload";
	case logstep_workload_report: return "workload";
	case logstep_workload_completed: return "workload";
#endif
	case logstep_workload_end: return "post-test";
	case logstep_after_sanity_post: return "shutdown";
	case logstep_qemu_exit: return "qemuexit";
	default: break;
	}

	fprintf(stderr, "error: invalid logstep %d\n", step);
	abort();
	return NULL;
}

const char *logstepname(enum logstep step) {
	switch (step) {
#define STEP(name) case logstep_##name: return #name;
	STEP(before_qemu_start)
	STEP(qemu_start)
	STEP(before_sanity_pre)
	STEP(after_sanity_pre)
#ifdef LOGSTEP_ALL
	STEP(workload_start)
	STEP(workload_report)
	STEP(workload_completed)
#endif
	STEP(workload_end)
	STEP(after_sanity_post)
	STEP(qemu_exit)
	STEP(count)
#undef step
	}

	fprintf(stderr, "error: invalid logstep %d\n", step);
	abort();
	return NULL;
}
