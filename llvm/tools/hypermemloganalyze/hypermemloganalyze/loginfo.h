#ifndef LOGINFO_H
#define LOGINFO_H

#include <sys/time.h>

#include "common.h"

enum logstep {
	logstep_before_qemu_start,
	logstep_qemu_start,
	logstep_before_sanity_pre,
	logstep_after_sanity_pre,
#ifdef LOGSTEP_ALL
	logstep_workload_start,
	logstep_workload_report,
	logstep_workload_completed,
#endif
	logstep_workload_end,
	logstep_after_sanity_post,
	logstep_qemu_exit,
	logstep_count,
};

struct loginfo_module {
	struct loginfo_module *next;
	char *name;
	struct timeval time_startup;
	struct timeval time_context_set;
	struct timeval time_faultindex_get;
	int fault_bb_index; /* one-based */
	int fault_bb_index_set;
	struct {
		struct timeval time;
	} stats[logstep_count];
#ifndef IGNORE_EXECCOUNTS
	int bb_count;
	execcount *execcount;
#endif
	unsigned fault_count[logstep_count];
	struct timeval fault_time_first[logstep_count];
	struct timeval fault_time_last[logstep_count];
	unsigned restart_count[logstep_count];
	struct timeval restart_time_first[logstep_count];
	struct timeval restart_time_last[logstep_count];
};

struct loginfo {
	char *drive0;
	char *drive1;
	char *faultspec;
	char *logpath;
	char *system;
	const char *workload;
	char *workload_reported;
	int sanity_passed_pre;
	int sanity_passed_post;
	struct timeval step_time[logstep_count];
	struct loginfo_module *modules;
	enum logstep step;
};

void loginfo_free(struct loginfo *loginfo);
void loginfo_load_from_log(const char *logpath, struct loginfo *loginfo);
const char *logstepdesc(enum logstep step);
const char *logstepname(enum logstep step);

#endif
