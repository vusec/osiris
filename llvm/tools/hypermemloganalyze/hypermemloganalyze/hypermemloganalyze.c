#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "helper.h"
#include "loginfo.h"
#include "module.h"
#include "stats.h"

struct loginfo_list {
	struct loginfo_list *next;
	struct loginfo data;
};

static int log_count;

static struct loginfo_list *loginfo_golden_list;

static struct module_ll *modules;

static const char *option_output_prefix = "output";

static const char *option_source_path_prefix = "";

static const char *option_test_name;

static FILE *output_all;

static FILE *output_stattest_summary;

static const struct loginfo *golden_find(const struct loginfo *loginfo) {
	const struct loginfo_list *entry = loginfo_golden_list;

	assert(loginfo);

	while (entry) {
		if (safestrcmp(entry->data.system, loginfo->system) == 0 &&
			safestrcmp(entry->data.workload, loginfo->workload) == 0) {
			return &entry->data;
		}
		entry = entry->next;
	}
	return NULL;
}

static void load_map(const char *mappath) {
	module_load_from_map_ll(mappath, &modules);
}

static void print_time_field(FILE *file, const struct timeval *time,
	const struct timeval *reftime) {
	struct timeval timediff;

	assert(file);
	assert(time);
	assert(reftime);

	if (timeval_is_zero(time)) {
		fprintf(file, "\t");
		return;
	}

	timediff = *time;
	timeval_subtract(&timediff, reftime);
	if (time->tv_usec < 0 || time->tv_usec >= 1000000) {
		fprintf(stderr, "warning: invalid time specification: %ld.%.6d",
			(long) time->tv_sec, (int) time->tv_usec);
		fprintf(file, "\t");
		return;
	}

	fprintf(file, "%ld.%.6d\t", (long) timediff.tv_sec,
		(int) timediff.tv_usec);
}

static const struct module *module_map_find(
	const struct loginfo_module *module) {
	char *modname;
	const char *modname_end;
	size_t modnamelen;
	const struct module *module_map;

	/* remove instance from module name */
	modname_end = strchr(module->name, '@');
	modnamelen = modname_end ? (modname_end - module->name) :
		strlen(module->name);
	modname = MALLOC(modnamelen + 1, char);
	memcpy(modname, module->name, modnamelen);
	modname[modnamelen] = 0;

	/* look for module */
	module_map = module_find(modules, modname);

	/* clean up */
	FREE(modname);

	return module_map;
}

static void lookup_map_info(
	const struct loginfo_module *module,
	const char **faultpath,
	int *faultline,
	const char **faultfunc,
	const char **faulttype) {
	const struct module_func *func_map;
	const struct module *module_map;
	const struct module_bb *module_map_bb;

	assert(faultpath);
	assert(faultline);
	assert(faultfunc);
	assert(faulttype);

	*faultpath = NULL;
	*faultline = 0;
	*faultfunc = NULL;
	*faulttype = NULL;

	if (!module->fault_bb_index_set) return;

	module_map = module_map_find(module);
	if (!module_map) {
		fprintf(stderr, "warning: no map file specified for "
			"module %s\n", module->name);
		return;
	}

	/* fault_bb_index is one-based */
	module_map_bb = module_get_bb_by_index(module_map,
		module->fault_bb_index, &func_map);
	if (!module_map_bb) {
		fprintf(stderr, "warning: faulty bb %d of module %s not "
			"in map file\n", module->fault_bb_index, module->name);
		return;
	}

	*faultpath = module_map_bb->fault_path;
	*faultline = module_map_bb->fault_line;
	if (func_map) *faultfunc = func_map->name;
	*faulttype = module_map_bb->fault_type;
}

static const char *simplify_source_path(const char *s) {
	if (!s) return NULL;

	if (strncmp(s, option_source_path_prefix,
		strlen(option_source_path_prefix)) == 0) {
		return s + strlen(option_source_path_prefix);
	} else {
		return s;
	}
}

static void find_reftime(
	const struct loginfo *loginfo,
	struct timeval *reftime) {
	const struct loginfo_module *module;
	enum logstep step;

	assert(loginfo);
	assert(reftime);

	/* we use the earliest recorded time as a reference time to avoid negatives */
	memset(reftime, 0, sizeof(*reftime));
	for (step = 0; step < logstep_count; step++) {
		timeval_set_to_min(reftime, &loginfo->step_time[step]);
	}
	for (module = loginfo->modules; module; module = module->next) {
		timeval_set_to_min(reftime, &module->time_startup);
		timeval_set_to_min(reftime, &module->time_context_set);
		timeval_set_to_min(reftime, &module->time_faultindex_get);
		for (step = 0; step < logstep_count; step++) {
			timeval_set_to_min(reftime, &module->fault_time_last[step]);
			timeval_set_to_min(reftime, &module->restart_time_last[step]);
		}
	}
}

static struct loginfo_module *module_loginfo_find(const struct loginfo *loginfo,
	const char *name) {
	struct loginfo_module *module;

	assert(loginfo);
	assert(name);

	for (module = loginfo->modules; module; module = module->next) {
		if (strcmp(module->name, name) == 0) return module;
	}
	return NULL;
}

#ifndef IGNORE_EXECCOUNTS
static void match_bbs_executed(
	const struct loginfo_module *module,
	const struct loginfo_module *module_golden,
	int *bb_extra,
	int *bb_missing, int required) {
	const execcount *execcount, *execcount_golden;
	int i;

	assert(module);
	assert(module_golden);
	assert(bb_extra);
	assert(bb_missing);

	*bb_extra = 0;
	*bb_missing = 0;

	execcount = module->execcount;
	execcount_golden = module_golden->execcount;
	if (!execcount || !execcount_golden) {
		if (required || !execcount_golden) {
			fprintf(stderr, "warning: golden run has no execution "
				"counts for module %s\n", module->name);
		}
		return;
	}
	for (i = 0; i < module->bb_count; i++) {
		if (execcount[i] > 0 && execcount_golden[i] <= 0) {
			(*bb_extra)++;
		}
		if (execcount[i] <= 0 && execcount_golden[i] > 0) {
			(*bb_missing)++;
		}
	}
}
#endif

static int is_golden(const struct loginfo *loginfo) {
	int expect_gold;
	struct loginfo_module *module;

	assert(loginfo);

	expect_gold = !loginfo->faultspec || strcmp(loginfo->faultspec, "gold:42") == 0;
	for (module = loginfo->modules; module; module = module->next) {
		if (module->fault_bb_index_set) {
			if (expect_gold) {
				fprintf(stderr, "warning: golden run expected "
					"in log file %s but fault injected in "
					"module %s\n", loginfo->logpath,
					module->name);
			}
			return 0;
		}
	}
	if (!expect_gold) {
		fprintf(stderr, "warning: golden run not expected in log file "
			"%s due to faultspec \"%s\" but no faults actually "
			"injected\n", loginfo->logpath, loginfo->faultspec);
	}
	return 1;
}

static int find_last_step(const struct loginfo *loginfo, enum logstep *step_p) {
	int found = 0;
	enum logstep step;

	assert(loginfo);
	assert(step_p);

	*step_p = 0;
	for (step = 0; step < logstep_qemu_exit; step++) {
		if (!timeval_is_zero(&loginfo->step_time[step])) {
			*step_p = step;
			found = 1;
		}
	}

	return found;
}

static void output_analysis_all(const struct loginfo *loginfo) {
#ifndef IGNORE_EXECCOUNTS
	int bb_extra_module;
	int bb_missing_module;
#endif
	const char *faultfunc_map;
	const char *faultpath_map;
	int faultline_map;
	const char *faulttype_map;
	const struct loginfo *loginfo_golden;
	const struct loginfo_module *module;
	const struct loginfo_module *module_golden;
	int module_fault_act_count_current;
	int module_restart_count_current;
	int module_restart_count_current_golden;
	char *output_path;
	struct timeval reftime;
	enum logstep step;

	/* variables used to summarize module info */
	const char *faultfunc = NULL;
	int faultline = 0;
	const char *faultpath = NULL;
	const char *faulttype = NULL;
	int module_count = 0;
	int module_startup_count = 0;
	struct timeval module_startup_time_first = {};
	struct timeval module_startup_time_last = {};
	int module_context_set_count = 0;
	struct timeval module_context_set_time_first = {};
	struct timeval module_context_set_time_last = {};
	int module_faultindex_get_count = 0;
	struct timeval module_faultindex_get_time_first = {};
	struct timeval module_faultindex_get_time_last = {};
	int module_fault_inj_count = 0;
	int module_fault_act_count = 0;
	int fault_act_count = 0;
	struct timeval fault_act_time_first = {};
	struct timeval fault_act_time_last = {};
	int fault_act_count_step[logstep_count];
	int module_restart_count = 0;
	int restart_count = 0;
	struct timeval restart_time_first = {};
	struct timeval restart_time_last = {};
	int restart_count_step[logstep_count];
	int module_restart_count_not_in_golden = 0;
	int restart_count_not_in_golden = 0;
#ifndef IGNORE_EXECCOUNTS
	int bb_extra = 0;
	int bb_missing = 0;
#endif
	int module_extra = 0;
	int module_missing = 0;
	memset(fault_act_count_step, 0, sizeof(fault_act_count_step));
	memset(restart_count_step, 0, sizeof(restart_count_step));

	assert(loginfo);

	/* open file if needed */
	if (!output_all) {
		output_path = ASPRINTF("%s-all.txt", option_output_prefix);
		output_all = fopen(output_path, "w");
		if (!output_all) {
			fprintf(stderr, "error: cannot open output file %s: %s\n",
				output_path, strerror(errno));
			exit(-1);
		}
		FREE(output_path);

		/* write header */
		if (option_test_name) fprintf(output_all, "testname\t");
		fprintf(output_all,
			"system\t"
			"workload\t"
			"logpath\t"
			"faultspec\t"
			"faultloc\t"
			"faultfunc\t"
			"faultype\t"
			"workload_reported\t"
			"sanity_passed_pre\t"
			"sanity_passed_post\t"
			"last_step\t");
		for (step = logstep_qemu_start; step < logstep_count; step++) {
			fprintf(output_all, "time_%s\t", logstepname(step));
		}
		fprintf(output_all,
			"module_count\t"
			"module_startup_count\t"
			"module_startup_time_first\t"
			"module_startup_time_last\t"
			"module_context_set_count\t"
			"module_context_set_time_first\t"
			"module_context_set_time_last\t"
			"module_faultindex_get_count\t"
			"module_faultindex_get_time_first\t"
			"module_faultindex_get_time_last\t"
			"module_fault_inj_count\t"
			"module_fault_act_count\t"
			"fault_act_count\t"
			"fault_act_time_first\t"
			"fault_act_time_last\t");
		for (step = 0; step < logstep_count; step++) {
			fprintf(output_all, "fault_act_count_%s\t", logstepname(step));
		}
		fprintf(output_all,
			"module_restart_count\t"
			"restart_count\t"
			"restart_time_first\t"
			"restart_time_last\t");
		for (step = 0; step < logstep_count; step++) {
			fprintf(output_all, "restart_count_%s\t", logstepname(step));
		}
		fprintf(output_all,
			"module_restart_count_not_in_golden\t"
			"restart_count_not_in_golden\t"
#ifndef IGNORE_EXECCOUNTS
			"bb_extra\t"
			"bb_missing\t"
#endif
			"module_extra\t"
			"module_missing\n");
	}

	/* summarize module info */
	find_reftime(loginfo, &reftime);
	loginfo_golden = is_golden(loginfo) ? NULL : golden_find(loginfo);
	for (module = loginfo->modules; module; module = module->next) {
		module_golden = loginfo_golden ?
			module_loginfo_find(loginfo_golden, module->name) : NULL;
	
		/* fields taken from map file */
		lookup_map_info(module, &faultpath_map, &faultline_map,
			&faultfunc_map, &faulttype_map);
		if (faultpath_map) {
			if (!faultpath) {
				faultpath = faultpath_map;
			} else if (strcmp(faultpath, faultpath_map) != 0) {
				fprintf(stderr, "warning: inconsistent fault "
					"paths %s and %s for module %s in "
					"log file %s\n", faultpath,
					faultpath_map, module->name,
					loginfo->logpath);
			}
		}
		if (faultline_map) {
			if (!faultline) {
				faultline = faultline_map;
			} else if (faultline != faultline_map) {
				fprintf(stderr, "warning: inconsistent fault "
					"lines %d and %d for module %s in "
					"log file %s\n", faultline,
					faultline_map, module->name,
					loginfo->logpath);
			}
		}
		if (faultfunc_map) {
			if (!faultfunc) {
				faultfunc = faultfunc_map;
			} else if (strcmp(faultfunc, faultfunc_map) != 0) {
				fprintf(stderr, "warning: inconsistent fault "
					"locations %s and %s for module %s in "
					"log file %s\n", faultfunc,
					faultfunc_map, module->name,
					loginfo->logpath);
			}
		}
		if (faulttype_map) {
			if (!faulttype) {
				faulttype = faulttype_map;
			} else if (strcmp(faulttype, faulttype_map) != 0) {
				fprintf(stderr, "warning: inconsistent fault "
					"types %s and %s for module %s in "
					"log file %s\n", faulttype,
					faulttype_map, module->name,
					loginfo->logpath);
			}
		}

		/* fields taken from log */
		module_count++;
		if (!timeval_is_zero(&module->time_startup)) {
			module_startup_count++;
			timeval_set_to_min(&module_startup_time_first, &module->time_startup);
			timeval_set_to_max(&module_startup_time_last, &module->time_startup);
		}
		if (!timeval_is_zero(&module->time_context_set)) {
			module_context_set_count++;
			timeval_set_to_min(&module_context_set_time_first, &module->time_context_set);
			timeval_set_to_max(&module_context_set_time_last, &module->time_context_set);
		}
		if (!timeval_is_zero(&module->time_faultindex_get)) {
			module_faultindex_get_count++;
			timeval_set_to_min(&module_faultindex_get_time_first, &module->time_faultindex_get);
			timeval_set_to_max(&module_faultindex_get_time_last, &module->time_faultindex_get);
		}
		if (module->fault_bb_index_set) module_fault_inj_count++;

		module_fault_act_count_current = 0;
		module_restart_count_current = 0;
		module_restart_count_current_golden = 0;
		for (step = 0; step < logstep_count; step++) {
			module_fault_act_count_current += module->fault_count[step];
			fault_act_count += module->fault_count[step];
			timeval_set_to_min(&fault_act_time_first, &module->fault_time_first[step]);
			timeval_set_to_max(&fault_act_time_last, &module->fault_time_last[step]);
			fault_act_count_step[step] += module->fault_count[step];
			module_restart_count_current += module->restart_count[step];
			if (module_golden) module_restart_count_current_golden += module_golden->restart_count[step];
			restart_count += module->restart_count[step];
			timeval_set_to_min(&restart_time_first, &module->restart_time_first[step]);
			timeval_set_to_max(&restart_time_last, &module->restart_time_last[step]);
			restart_count_step[step] += module->restart_count[step];
		}
		if (module_fault_act_count_current > 0) module_fault_act_count++;
		if (module_restart_count_current > 0) module_restart_count++;
		if (module_restart_count_current > module_restart_count_current_golden) {
			module_restart_count_not_in_golden++;
			restart_count_not_in_golden += module_restart_count_current -
				module_restart_count_current_golden;
		}
		if (module_golden) {
#ifndef IGNORE_EXECCOUNTS
			match_bbs_executed(module, module_golden, &bb_extra_module,
				&bb_missing_module, 0);
			bb_extra += bb_extra_module;
			bb_missing += bb_missing_module;
#endif
		} else {
			module_extra++;
		}
	}
	if (loginfo_golden) {
		for (module_golden = loginfo_golden->modules;
			module_golden; module_golden = module_golden->next) {
			module = module_loginfo_find(loginfo,
				module_golden->name);
			if (!module) module_missing++;
		}
	}

	if (module_fault_inj_count && is_golden(loginfo)) {
		fprintf(stderr, "warning: faults injected in golden run "
			"from log file %s\n", loginfo->logpath);
	}

	/* write line */
	if (option_test_name) fprintf(output_all, "%s\t", option_test_name);
	fprintf(output_all, "%s\t", loginfo->system ? : "");
	fprintf(output_all, "%s\t", loginfo->workload ? : "");
	fprintf(output_all, "%s\t", loginfo->logpath);
	fprintf(output_all, "%s\t", (is_golden(loginfo) || !loginfo->faultspec) ? "" : loginfo->faultspec);
	fprintf(output_all, "%s", simplify_source_path(faultpath) ? : "");
	if (faultline) fprintf(output_all, ":%d", faultline);
	fprintf(output_all, "\t");
	fprintf(output_all, "%s\t", faultfunc ? : "");
	fprintf(output_all, "%s\t", faulttype ? : "");
	fprintf(output_all, "%s\t", loginfo->workload_reported ? : "");
	fprintf(output_all, "%d\t", loginfo->sanity_passed_pre);
	fprintf(output_all, "%d\t", loginfo->sanity_passed_post);
	if (find_last_step(loginfo, &step)) {
		fprintf(output_all, "%s", logstepname(step));
	}
	fprintf(output_all, "\t");
	for (step = logstep_qemu_start; step < logstep_count; step++) {
		print_time_field(output_all, &loginfo->step_time[step], &reftime);
	}
	fprintf(output_all, "%d\t", module_count);
	fprintf(output_all, "%d\t", module_startup_count);
	print_time_field(output_all, &module_startup_time_first, &reftime);
	print_time_field(output_all, &module_startup_time_last, &reftime);
	fprintf(output_all, "%d\t", module_context_set_count);
	print_time_field(output_all, &module_context_set_time_first, &reftime);
	print_time_field(output_all, &module_context_set_time_last, &reftime);
	fprintf(output_all, "%d\t", module_faultindex_get_count);
	print_time_field(output_all, &module_faultindex_get_time_first, &reftime);
	print_time_field(output_all, &module_faultindex_get_time_last, &reftime);
	fprintf(output_all, "%d\t", module_fault_inj_count);
	fprintf(output_all, "%d\t", module_fault_act_count);
	fprintf(output_all, "%d\t", fault_act_count);
	print_time_field(output_all, &fault_act_time_first, &reftime);
	print_time_field(output_all, &fault_act_time_last, &reftime);
	for (step = 0; step < logstep_count; step++) {
		fprintf(output_all, "%d\t", fault_act_count_step[step]);
	}
	fprintf(output_all, "%d\t", module_restart_count);
	fprintf(output_all, "%d\t", restart_count);
	print_time_field(output_all, &restart_time_first, &reftime);
	print_time_field(output_all, &restart_time_last, &reftime);
	for (step = 0; step < logstep_count; step++) {
		fprintf(output_all, "%d\t", restart_count_step[step]);
	}
	if (!is_golden(loginfo)) {
		fprintf(output_all, "%d\t", module_restart_count_not_in_golden);
		fprintf(output_all, "%d\t", restart_count_not_in_golden);
#ifndef IGNORE_EXECCOUNTS
		fprintf(output_all, "%d\t", bb_extra);
		fprintf(output_all, "%d\t", bb_missing);
#endif
		fprintf(output_all, "%d\t", module_extra);
		fprintf(output_all, "%d\n", module_missing);
	} else {
		fprintf(output_all, "\t");
		fprintf(output_all, "\t");
#ifndef IGNORE_EXECCOUNTS
		fprintf(output_all, "\t");
		fprintf(output_all, "\t");
#endif
		fprintf(output_all, "\t");
		fprintf(output_all, "\n");
	}
}

#define BIN_COUNT 12

enum outcome {
	outcome_inactive,
	outcome_invalid,
	outcome_success,
	outcome_crash,
	outcome_count,
};

static const char *outcomename(enum outcome outcome) {
	switch (outcome) {
	case outcome_inactive: return "inactive";
	case outcome_invalid: return "invalid";
	case outcome_success: return "success";
	case outcome_crash: return "crash";
	default: break;
	}

	fprintf(stderr, "error: invalid outcome %d\n", outcome);
	abort();
	return NULL;
}

struct stats_aggregate {
	int n;
	int sanity_passed_neither;
	int sanity_passed_pre;
	int sanity_passed_post;
	int sanity_passed_both;
	int activated;
	int valid;
	int crash;
	int step_reached[outcome_count][logstep_count];
	struct stat_aggregate_double step_time_diff[logstep_count][logstep_count];
	struct stat_aggregate_zscore step_time_diff_z[logstep_count][logstep_count];
	struct stat_aggregate_int module_fault_act_count;
	struct stat_aggregate_int fault_act_count;
	struct stat_aggregate_double fault_act_count_log10;
	int fault_act_count_bins[BIN_COUNT];
	struct stat_aggregate_int module_restart_count;
	struct stat_aggregate_int restart_count;
	int restart_count_bins[BIN_COUNT];
};

struct stats_aggregate_per_test {
	struct stats_aggregate_per_test *next;
	char *system;
	char *workload;
	struct stats_aggregate stats;
};

struct stats_by_string_key {
	struct stats_by_string_key *next;
	const char *key;
	struct stats_aggregate_per_test *stats_list;
};

struct stats_by_string_key_per_test {
	const char *key;
	struct stats_aggregate_per_test *stats_struct;
};

static struct stats_aggregate_per_test *stats_golden;
static struct stats_by_string_key *stats_per_fault_act_count;
static struct stats_by_string_key *stats_per_fault_type;
static struct stats_by_string_key *stats_per_module;
static struct stats_by_string_key *stats_per_path;
static struct stats_by_string_key *stats_per_pathclass;
static struct stats_by_string_key *stats_per_restart_count;
static struct stats_by_string_key *stats_per_step;

static double timeval_diff(const struct timeval *time1, const struct timeval *time2) {
	return (time1->tv_sec - time2->tv_sec) +
		(time1->tv_usec - time2->tv_usec) / 1000000.0;
}

static struct pathclass_spec {
	const char *pathclass;
	const char *path;
} pathclass_specs[] = {
	{ "driver", "/home/skyel/mnt/git/llvm-apps/apps/minix/minix/drivers" },
	{ "driver", "drivers" },
	{ "fs", "/home/skyel/mnt/git/llvm-apps/apps/minix/minix/servers/mfs" },
	{ "fs", "/home/skyel/mnt/git/llvm-apps/apps/minix/minix/servers/pfs" },
	{ "fs", "/home/skyel/mnt/git/llvm-apps/apps/minix/minix/servers/procfs" },
	{ "fs", "fs" },
	{ "kernel", "/home/skyel/mnt/git/llvm-apps/apps/minix/minix/kernel" },
	{ "kernel", "arch/x86/kernel" },
	{ "kernel", "kernel" },
	{ "lib", "/home/skyel/mnt/git/llvm-apps/apps/minix/minix/../obj.i386/destdir.i386/usr/include" },
	{ "lib", "/home/skyel/mnt/git/llvm-apps/apps/minix/minix/common/lib" },
	{ "lib", "/home/skyel/mnt/git/llvm-apps/apps/minix/minix/lib" },
	{ "lib", "/home/skyel/mnt/git/llvm-apps/apps/minix/minix/sys/lib" },
	{ "lib", "/var/scratch/rge280/llvm-apps/apps/linux/src/arch/x86/include" },
	{ "lib", "include" },
	{ "lib", "lib" },
	{ "mm", "/home/skyel/mnt/git/llvm-apps/apps/minix/minix/servers/vm" },
	{ "mm", "arch/x86/mm" },
	{ "mm", "mm" },
	{ "net", "/home/skyel/mnt/git/llvm-apps/apps/minix/minix/servers/inet" },
	{ "net", "net" },
	{ "pm", "/home/skyel/mnt/git/llvm-apps/apps/minix/minix/servers/pm" },
	{ "server", "/home/skyel/mnt/git/llvm-apps/apps/minix/minix/servers" },
	{ "vfs", "/home/skyel/mnt/git/llvm-apps/apps/minix/minix/servers/vfs" },
};

static int path_match(const char *path, const char *matchwith) {
	size_t matchwithlen, pathlen;

	assert(path);
	assert(matchwith);

	/* path must either equal matchwith or start with matchwith followed
	 * by a directory separator
	 */
	matchwithlen = strlen(matchwith);
	pathlen = strlen(path);
	if (pathlen < matchwithlen) return 0;
	if (memcmp(path, matchwith, matchwithlen) != 0) return 0;
	if (pathlen == matchwithlen) return 1;
	if (path[matchwithlen] == '/') return 1;
	return 0;
}

#define LENGTH(array) (sizeof((array)) / sizeof((array)[0]))

static const char *pathclassify(const char *path) {
	const char *pathclass = NULL;
	int i;
	const char *matchedwith = NULL;
	const char *matchwith;

	assert(path);

	/* select the longest match */
	for (i = 0; i < LENGTH(pathclass_specs); i++) {
		matchwith = pathclass_specs[i].path;
		if (!path_match(path, matchwith)) continue;
		if (matchedwith && strlen(matchedwith) >= strlen(matchwith)) continue;
		pathclass = pathclass_specs[i].pathclass;
		matchedwith = matchwith;
	}

	return pathclass ? : "other";
}

static int bin_index(int value) {
	if (value < 1) return 0;
	if (value < 2) return 1;
	if (value < 5) return 2;
	if (value < 10) return 3;
	if (value < 100) return 4;
	if (value < 1000) return 5;
	if (value < 10000) return 6;
	if (value < 100000) return 7;
	if (value < 1000000) return 8;
	if (value < 10000000) return 9;
	if (value < 100000000) return 10;
	return 11;
}

static const char *bin_desc(int bindex) {
	switch (bindex) {
	case 0: return "0";
	case 1: return "1";
	case 2: return "2_4";
	case 3: return "5_9";
	case 4: return "10_99";
	case 5: return "100_999";
	case 6: return "1000_9999";
	case 7: return "10000_99999";
	case 8: return "100000_999999";
	case 9: return "1000000_9999999";
	case 10: return "10000000_99999999";
	case 11: return "100000000_and_more";
	default: abort(); return NULL;
	}
}

static struct stats_aggregate *get_stats_golden(
	const char *system,
	const char *workload) {
	struct stats_aggregate_per_test *stats;

	stats = stats_golden;
	while (stats) {
		if (safestrcmp(stats->system, system) == 0 &&
			safestrcmp(stats->workload, workload) == 0) {
			return &stats->stats;
		}
		stats = stats->next;
	}
	return NULL;
}

static void output_analysis_aggregate_add(
	const struct loginfo *loginfo,
	struct stats_aggregate *stats,
	int use_golden) {
	int fault_act_count = 0;
	struct loginfo_module *module;
	int module_fault_act_count = 0;
	int module_fault_act_count_current;
	int module_restart_count = 0;
	int module_restart_count_current;
	int restart_count = 0;
	enum outcome outcome;
	enum logstep step, step1, step2, step_reached;
	const struct timeval *time1, *time2;
	struct stats_aggregate *stats_golden;
	struct stat_aggregate_double *step_time_diff_golden;

	assert(loginfo);
	assert(stats);

	stats_golden = use_golden ?
		get_stats_golden(loginfo->system, loginfo->workload) : NULL;

	stats->n++;
	if (loginfo->sanity_passed_pre) {
		if (loginfo->sanity_passed_post) {
			stats->sanity_passed_both++;
		} else {
			stats->sanity_passed_pre++;
		}
	} else {
		if (loginfo->sanity_passed_post) {
			stats->sanity_passed_post++;
		} else {
			stats->sanity_passed_neither++;
		}
	}
	step_reached = 0;
	for (step1 = 0; step1 < logstep_count; step1++) {
		time1 = &loginfo->step_time[step1];
		if (timeval_is_zero(time1)) continue;
		if (step1 < logstep_qemu_exit) step_reached = step1;
		for (step2 = step1 + 1; step2 < logstep_count; step2++) {
			time2 = &loginfo->step_time[step2];
			if (timeval_is_zero(time2)) continue;
			stat_aggregate_double_add(
				&stats->step_time_diff[step1][step2],
				timeval_diff(time2, time1));
			if (stats_golden) {
				step_time_diff_golden = &stats_golden->step_time_diff[step1][step2];
				if (step_time_diff_golden->n <= 1) {
					fprintf(stderr, "warning: not enough "
						"golden runs to compare step "
						"%s -> %s timing against for "
						"Z-score for log %s\n",
						logstepname(step1),
						logstepname(step2),
						loginfo->logpath);
				} else {
					stat_aggregate_zscore_add(
						&stats->step_time_diff_z[step1][step2],
						timeval_diff(time2, time1),
						step_time_diff_golden);
				}
			}
		}
	}
	for (module = loginfo->modules; module; module = module->next) {
		module_fault_act_count_current = 0;
		module_restart_count_current = 0;
		for (step = 0; step < logstep_count; step++) {
			if (module->fault_count[step] > 0) {
				module_fault_act_count_current = 1;
				fault_act_count += module->fault_count[step];
			}
			if (module->restart_count[step] > 0) {
				module_restart_count_current = 1;
				restart_count += module->restart_count[step];
			}
		}
		module_fault_act_count += module_fault_act_count_current;
		module_restart_count += module_restart_count_current;
	}
	stat_aggregate_int_add(&stats->module_fault_act_count,
		module_fault_act_count);
	stat_aggregate_int_add(&stats->fault_act_count, fault_act_count);
	if (fault_act_count > 0) stat_aggregate_double_add(&stats->fault_act_count_log10, log10(fault_act_count));
	stats->fault_act_count_bins[bin_index(fault_act_count)]++;
	stat_aggregate_int_add(&stats->module_restart_count,
		module_restart_count);
	stat_aggregate_int_add(&stats->restart_count, restart_count);
	stats->restart_count_bins[bin_index(restart_count)]++;
	if (fault_act_count > 0) {
		stats->activated++;
		if (loginfo->sanity_passed_pre) {
			stats->valid++;
			if (!loginfo->sanity_passed_post) {
				stats->crash++;
				outcome = outcome_crash;
			} else {
				outcome = outcome_success;
			}
		} else {
			outcome = outcome_invalid;
		}
	} else {
		outcome = outcome_inactive;
	}
	stats->step_reached[outcome][step_reached]++;
}

static void output_analysis_aggregate_golden(const struct loginfo *loginfo);

static void output_analysis_aggregate_golden_all(void) {
	const struct loginfo_list *node;

	for (node = loginfo_golden_list; node; node = node->next) {
		output_analysis_aggregate_golden(&node->data);
	}
}

static void output_analysis_aggregate_add_for_test(
	const struct loginfo *loginfo,
	struct stats_aggregate_per_test **stats_p,
	const char *system,
	const char *workload,
	int use_golden) {
	struct stats_aggregate_per_test *stats;

	assert(loginfo);
	assert(stats_p);

	while ((stats = *stats_p)) {
		if (safestrcmp(stats->system, system) == 0 &&
			safestrcmp(stats->workload, workload) == 0) {
			goto found;
		}
		stats_p = &stats->next;
	}

	stats = *stats_p = CALLOC(1, struct stats_aggregate_per_test);
	if (system) stats->system = STRDUP(system);
	if (workload) stats->workload = STRDUP(workload);

found:
	output_analysis_aggregate_add(loginfo, &stats->stats, use_golden);
}

static void output_analysis_aggregate_add_per_test(
	const struct loginfo *loginfo,
	struct stats_aggregate_per_test **stats_p,
	int use_golden) {

	assert(loginfo);
	assert(stats_p);

	output_analysis_aggregate_add_for_test(loginfo, stats_p, NULL, NULL,
		use_golden);
	if (loginfo->system) {
		output_analysis_aggregate_add_for_test(loginfo, stats_p,
			loginfo->system, NULL, use_golden);
	}
	if (loginfo->workload) {
		output_analysis_aggregate_add_for_test(loginfo, stats_p,
			NULL, loginfo->workload, use_golden);
	}
	if (loginfo->system && loginfo->workload) {
		output_analysis_aggregate_add_for_test(loginfo, stats_p,
			loginfo->system, loginfo->workload, use_golden);
	}
}

static int strnumcmp(const char *s1, const char *s2) {
	char c1, c2;
	const char *n1, *n2;

	assert(s1);
	assert(s2);
	for (;;) {
		c1 = *s1;
		c2 = *s2;
		if (!c1) return c2 ? -1 : 0;
		if (!c2) return 1;
		if ('0' <= c1 && c1 <= '9' && '0' <= c2 && c2 <= '9') {
			while (*s1 == '0') s1++;
			while (*s2 == '0') s2++;
			n1 = s1;
			n2 = s2;
			while ('0' <= *s1 && *s1 <= '9') s1++;
			while ('0' <= *s2 && *s2 <= '9') s2++;
			if (s1 - n1 != s2 - n2) return (s1 - n1 < s2 - n2) ? -1 : 1;
			while (n1 < s1) {
				if (*n1 != *n2) return (*n1 < *n2) ? -1 : 1;
				n1++;
				n2++;
			}
		} else {
			if (c1 != c2) return (c1 < c2) ? -1 : 1;
			s1++;
			s2++;
		}
	}
}

static int keystrcmp(const char *s1, const char *s2) {
	if (!s1) return s2 ? -1 : 0;
	if (!s2) return 1;
	if (*s1 == '<' && *s2 != '<') return -1;
	if (*s1 != '<' && *s2 == '<') return 1;
	return strnumcmp(s1, s2);
}

static void output_analysis_aggregate_add_key(
	const struct loginfo *loginfo,
	const char *key,
	struct stats_by_string_key **list) {
	struct stats_by_string_key *node;

	assert(loginfo);
	assert(key);
	assert(list);

	for (node = *list; node; node = node->next) {
		if (keystrcmp(node->key, key) == 0) goto found;
	}

	node = CALLOC(1, struct stats_by_string_key);
	node->key = STRDUP(key);
	node->next = *list;
	*list = node;

found:
	output_analysis_aggregate_add_per_test(loginfo, &node->stats_list, 1);
}

static void output_analysis_aggregate_fault_act_count(
	const struct loginfo *loginfo,
	const char *desc, int count) {
	if (desc == NULL) desc = bin_desc(bin_index(count));

	output_analysis_aggregate_add_key(loginfo, desc, &stats_per_fault_act_count);
}

static void output_analysis_aggregate_fault_type(
	const struct loginfo *loginfo,
	const char *fault_type) {
	output_analysis_aggregate_add_key(loginfo, fault_type, &stats_per_fault_type);
}

static void output_analysis_aggregate_module(
	const struct loginfo *loginfo,
	const char *module) {
	output_analysis_aggregate_add_key(loginfo, module, &stats_per_module);
}

static void output_analysis_aggregate_path(
	const struct loginfo *loginfo,
	const char *path) {
	output_analysis_aggregate_add_key(loginfo, path, &stats_per_path);
}

static void output_analysis_aggregate_pathclass(
	const struct loginfo *loginfo,
	const char *pathclass) {
	output_analysis_aggregate_add_key(loginfo, pathclass, &stats_per_pathclass);
}

static void output_analysis_aggregate_restart_count(
	const struct loginfo *loginfo,
	const char *desc,
	int count) {
	if (desc == NULL) desc = bin_desc(bin_index(count));

	output_analysis_aggregate_add_key(loginfo, desc, &stats_per_restart_count);
}

static const char *logstepdesc_aggregate(int step) {
	if (step < 0) {
		return "<ANY>";
	} else if (step >= logstep_count) {
		return "<NONE>";
	} else {
		return logstepdesc(step);
	}
}

static void output_analysis_aggregate_step_internal_str(
	const struct loginfo *loginfo,
	const char *step_fault_first) {
	char key[256];
	
	snprintf(key, sizeof(key), "%s", step_fault_first);
	output_analysis_aggregate_add_key(loginfo, key, &stats_per_step);
}

static void output_analysis_aggregate_step_internal(
	const struct loginfo *loginfo,
	int step_fault_first) {
	output_analysis_aggregate_step_internal_str(loginfo,
		logstepdesc_aggregate(step_fault_first));
}

static void output_analysis_aggregate_step(const struct loginfo *loginfo,
	const char *key, enum logstep step_fault_first) {

	if (key) {
		output_analysis_aggregate_add_key(loginfo, key, &stats_per_step);
		return;
	}

	output_analysis_aggregate_step_internal(loginfo, step_fault_first);
}

static void output_analysis_aggregate_all(const struct loginfo *loginfo,
	const char *key) {
	output_analysis_aggregate_fault_act_count(loginfo, key, 0);
	output_analysis_aggregate_fault_type(loginfo, key);
	output_analysis_aggregate_module(loginfo, key);
	output_analysis_aggregate_path(loginfo, key);
	output_analysis_aggregate_pathclass(loginfo, key);
	output_analysis_aggregate_restart_count(loginfo, key, 0);
	output_analysis_aggregate_step(loginfo, key, 0);
}

static size_t stats_aggregate_per_test_count(
	const struct stats_aggregate_per_test *list) {
	size_t count = 0;
	while (list) {
		count++;
		list = list->next;
	}
	return count;
}

static size_t stats_by_string_key_count(struct stats_by_string_key *list) {
	size_t count = 0;
	while (list) {
		count += stats_aggregate_per_test_count(list->stats_list);
		list = list->next;
	}
	return count;
}

static int stats_by_string_key_sort_compare(const void *p1, const void *p2) {
	struct stats_by_string_key_per_test *item1 = (struct stats_by_string_key_per_test *) p1;
	struct stats_by_string_key_per_test *item2 = (struct stats_by_string_key_per_test *) p2;
	int r;

	r = keystrcmp(item1->key, item2->key);
	if (r) return r;

	r = safestrcmp(item1->stats_struct->system,
		item2->stats_struct->system);
	if (r) return r;
	
	return safestrcmp(item1->stats_struct->workload,
		item2->stats_struct->workload);
}

static struct stats_by_string_key_per_test *stats_by_string_key_sort(
	struct stats_by_string_key *list, size_t count) {
	struct stats_by_string_key_per_test *sorted =
		MALLOC(count, struct stats_by_string_key_per_test);
	struct stats_by_string_key_per_test *sorted_item;
	struct stats_aggregate_per_test *sublist;

	sorted_item = sorted;
	while (list) {
		sublist = list->stats_list;
		while (sublist) {
			sorted_item->key = list->key;
			sorted_item->stats_struct = sublist;
			sorted_item++;
			sublist = sublist->next;
		}
		list = list->next;
	}

	qsort(sorted, count, sizeof(*sorted), stats_by_string_key_sort_compare);

	return sorted;
}

static size_t countchar(const char *s, char c) {
	size_t count = 0;
	while (*s) {
		if (*s == c) count++;
		s++;
	}
	return count;
}

static void print_double_field(FILE *file, double value, int precision) {
	if (isfinite(value)) fprintf(file, "%.*f", precision, value);
	fprintf(file, "\t");
}

static void output_analysis_aggregate_write_time_diff(
	FILE *file,
	struct stats_aggregate *stats,
	enum logstep step1,
	enum logstep step2) {
	struct stat_aggregate_zscore *stats_z;
	struct stat_aggregate_double *step_time_diff;
	int z;

	if (stats) {
		step_time_diff = &stats->step_time_diff[step1][step2];
		fprintf(file, "%d\t", step_time_diff->n);
		print_double_field(file, stat_aggregate_double_avg(step_time_diff), 6);
		print_double_field(file, stat_aggregate_double_stdev(step_time_diff), 6);
	} else {
		fprintf(file,
			"time_%1$s_to_%2$s_n\t"
			"time_%1$s_to_%2$s_avg\t"
			"time_%1$s_to_%2$s_stdev\t",
			logstepname(step1), logstepname(step2));
	}

	for (z = -ZSCORE_MAX; z <= ZSCORE_MAX; z++) {
		if (!stats) {
			if (z == -ZSCORE_MAX) {
				fprintf(file, "time_%s_to_%s_z_less_m%d\t", logstepname(step1), logstepname(step2), ZSCORE_MAX);
			} else if (z == ZSCORE_MAX) {
				fprintf(file, "time_%s_to_%s_z_greater_p%d\t", logstepname(step1), logstepname(step2), ZSCORE_MAX);
			} else if (z < 0) {
				fprintf(file, "time_%s_to_%s_z_m%d_to_m%d\t", logstepname(step1), logstepname(step2), -z + 1, -z);
			} else if (z > 0) {
				fprintf(file, "time_%s_to_%s_z_p%d_to_p%d\t", logstepname(step1), logstepname(step2), z, z + 1);
			} else {
				fprintf(file, "time_%s_to_%s_z_m1_to_p1\t", logstepname(step1), logstepname(step2));
			}
		} else {
			stats_z = &stats->step_time_diff_z[step1][step2];
			if (!stat_aggregate_zscore_is_zero(stats_z)) {
				fprintf(file, "%d", stats_z->bins[z + ZSCORE_MAX]);
			}
			fprintf(file, "\t");
		}
	}
}

static void output_analysis_aggregate_write_time_diffs(
	FILE *file,
	struct stats_aggregate *stats) {
	output_analysis_aggregate_write_time_diff(file, stats, logstep_qemu_start, logstep_before_sanity_pre);
	output_analysis_aggregate_write_time_diff(file, stats, logstep_before_sanity_pre, logstep_after_sanity_pre);
	output_analysis_aggregate_write_time_diff(file, stats, logstep_after_sanity_pre, logstep_workload_end);
	output_analysis_aggregate_write_time_diff(file, stats, logstep_workload_end, logstep_after_sanity_post);
	output_analysis_aggregate_write_time_diff(file, stats, logstep_qemu_start, logstep_after_sanity_post);
}

static void print_key_with_colcount(FILE *file, const char *key,
	size_t colcount) {
	size_t colcountkey, colindex;

	assert(file);
	assert(key);
	assert(colcount >= 1);

	colcountkey = countchar(key, '\t') + 1;
	if (colcountkey == 1) {
		for (colindex = 0; colindex < colcount; colindex++) {
			fprintf(file, "%s\t", key);
		}
	} else {
		assert(colcountkey == colcount);
		fprintf(file, "%s\t", key);
	}
}

static void output_analysis_aggregate_write_cols(const char *name,
	const char **cols, struct stats_by_string_key *list, int with_depth) {
	size_t bindex, count, i;
	size_t colcount = 0;
	FILE *file;
	char *path;
	enum outcome outcome;
	struct stats_by_string_key_per_test *sorted;
	struct stats_aggregate *stats;
	enum logstep step;

	assert(name);
	assert(cols);

	count = stats_by_string_key_count(list);
	if (count <= 0) return;

	path = ASPRINTF("%s-%s.txt", option_output_prefix, name);
	file = fopen(path, "w");
	if (!file) {
		fprintf(stderr, "error: cannot open output file %s: %s\n",
			path, strerror(errno));
		exit(-1);
	}
	FREE(path);

	if (option_test_name) fprintf(file, "testname\t");
	fprintf(file, "system\t");
	fprintf(file, "workload\t");
	if (with_depth) fprintf(file, "%s_depth\t", name);
	while (*cols) {
		fprintf(file, "%s\t", *(cols++));
		colcount++;
	}
	fprintf(file,
		"n\t"
		"sanity_passed_neither\t"
		"sanity_passed_pre\t"
		"sanity_passed_post\t"
		"sanity_passed_both\t"
		"activated_n\t"
		"valid_n\t"
		"crash_n\t"
		"valid_pct\t"
		"crash_pct\t");
	for (outcome = 0; outcome < outcome_count; outcome++) {
		for (step = 0; step < logstep_qemu_exit; step++) {
			fprintf(file, "reached_%s_%s\t",
				outcomename(outcome), logstepdesc(step));
		}
	}
	fprintf(file,
		"module_fault_act_count_avg\t"
		"module_fault_act_count_stdev\t"
		"fault_act_count_avg\t"
		"fault_act_count_stdev\t"
		"fault_act_count_log10_n\t"
		"fault_act_count_log10_avg\t"
		"fault_act_count_log10_stdev\t");
	for (bindex = 0; bindex < BIN_COUNT; bindex++) {
		fprintf(file, "fault_act_count_%s\t", bin_desc(bindex));
	}
	fprintf(file,
		"module_restart_count_avg\t"
		"module_restart_count_stdev\t"
		"restart_count_avg\t"
		"restart_count_stdev\t");
	for (bindex = 0; bindex < BIN_COUNT; bindex++) {
		fprintf(file, "restart_count_%s\t", bin_desc(bindex));
	}
	output_analysis_aggregate_write_time_diffs(file, NULL);
	fprintf(file, "\n");

	sorted = stats_by_string_key_sort(list, count);
	for (i = 0; i < count; i++) {
		stats = &sorted[i].stats_struct->stats;
		if (option_test_name) fprintf(file, "%s\t", option_test_name);
		fprintf(file, "%s\t", sorted[i].stats_struct->system ? : "<ALL>");
		fprintf(file, "%s\t", sorted[i].stats_struct->workload ? : "<ALL>");
		if (with_depth) fprintf(file, "%d\t", (int) countchar(sorted[i].key, '/'));

		print_key_with_colcount(file, sorted[i].key, colcount);
		fprintf(file, "%d\t", stats->n);
		fprintf(file, "%d\t", stats->sanity_passed_neither);
		fprintf(file, "%d\t", stats->sanity_passed_pre);
		fprintf(file, "%d\t", stats->sanity_passed_post);
		fprintf(file, "%d\t", stats->sanity_passed_both);
		fprintf(file, "%d\t", stats->activated);
		fprintf(file, "%d\t", stats->valid);
		fprintf(file, "%d\t", stats->crash);
		
		if (stats->activated > 0) {
			print_double_field(file, stats->valid * 100.0 / stats->activated, 2);
		} else {
			fprintf(file, "\t");
		}
		if (stats->valid > 0) {
			print_double_field(file, stats->crash * 100.0 / stats->valid, 2);
		} else {
			fprintf(file, "\t");
		}
		
		for (outcome = 0; outcome < outcome_count; outcome++) {
			for (step = 0; step < logstep_qemu_exit; step++) {
				fprintf(file, "%d\t", stats->step_reached[outcome][step]);
			}
		}
		assert(stats->module_fault_act_count.n == stats->n);
		print_double_field(file, stat_aggregate_int_avg(&stats->module_fault_act_count), 6);
		print_double_field(file, stat_aggregate_int_stdev(&stats->module_fault_act_count), 6);
		assert(stats->fault_act_count.n == stats->n);
		print_double_field(file, stat_aggregate_int_avg(&stats->fault_act_count), 6);
		print_double_field(file, stat_aggregate_int_stdev(&stats->fault_act_count), 6);
		fprintf(file, "%d\t", stats->fault_act_count_log10.n);
		print_double_field(file, stat_aggregate_double_avg(&stats->fault_act_count_log10), 6);
		print_double_field(file, stat_aggregate_double_stdev(&stats->fault_act_count_log10), 6);
		for (bindex = 0; bindex < BIN_COUNT; bindex++) {
			fprintf(file, "%d\t", stats->fault_act_count_bins[bindex]);
		}
		assert(stats->module_restart_count.n == stats->n);
		print_double_field(file, stat_aggregate_int_avg(&stats->module_restart_count), 6);
		print_double_field(file, stat_aggregate_int_stdev(&stats->module_restart_count), 6);
		assert(stats->restart_count.n == stats->n);
		print_double_field(file, stat_aggregate_int_avg(&stats->restart_count), 6);
		print_double_field(file, stat_aggregate_int_stdev(&stats->restart_count), 6);
		for (bindex = 0; bindex < BIN_COUNT; bindex++) {
			fprintf(file, "%d\t", stats->restart_count_bins[bindex]);
		}
		output_analysis_aggregate_write_time_diffs(file, stats);
		fprintf(file, "\n");
	}

	FREE(sorted);
	fclose(file);
}

static void output_analysis_aggregate_write(const char *name,
	struct stats_by_string_key *list, int with_depth) {
	const char *cols[] = { name, NULL };

	output_analysis_aggregate_write_cols(name, cols, list, with_depth);
}

static void output_analysis_aggregate_write_all(void) {
	const char *cols_step[] = { "fault_first", NULL };

	output_analysis_aggregate_write("fault_act_count", stats_per_fault_act_count, 0);
	output_analysis_aggregate_write("fault_type", stats_per_fault_type, 0);
	output_analysis_aggregate_write("module", stats_per_module, 0);
	output_analysis_aggregate_write("path", stats_per_path, 1);
	output_analysis_aggregate_write("pathclass", stats_per_pathclass, 0);
	output_analysis_aggregate_write("restart_count", stats_per_restart_count, 0);
	output_analysis_aggregate_write_cols("step", cols_step, stats_per_step, 0);
}

static int stattest_stats_aggregate_per_test_sort_compare(const void *p1, const void *p2) {
	struct stats_aggregate_per_test *s1 = *(struct stats_aggregate_per_test **) p1;
	struct stats_aggregate_per_test *s2 = *(struct stats_aggregate_per_test **) p2;
	int r;

	r = safestrcmp(s1->system, s2->system);
	if (r) return r;

	return safestrcmp(s1->workload, s2->workload);
}

static int stattest_stats_by_string_key_sort_compare(const void *p1, const void *p2) {
	struct stats_by_string_key *s1 = *(struct stats_by_string_key **) p1;
	struct stats_by_string_key *s2 = *(struct stats_by_string_key **) p2;

	return keystrcmp(s1->key, s2->key);
}

static struct stats_aggregate_per_test **stattest_stats_aggregate_per_test_sort(
	struct stats_aggregate_per_test *list, size_t *count_p) {
	size_t count = 0;
	struct stats_aggregate_per_test *entry;
	struct stats_aggregate_per_test **sorted, **sorted_entry;

	assert(count_p);

	for (entry = list; entry; entry = entry->next) {
		count++;
	}

	sorted = CALLOC(count, struct stats_aggregate_per_test *);
	sorted_entry = sorted;
	for (entry = list; entry; entry = entry->next) {
		*(sorted_entry++) = entry;
	}

	qsort(sorted, count, sizeof(struct stats_aggregate_per_test *),
		stattest_stats_aggregate_per_test_sort_compare);

	*count_p = count;
	return sorted;
}

static const struct stats_by_string_key **stattest_stats_by_string_key_sort(
	const struct stats_by_string_key *list,
	size_t *count_p) {
	size_t count = 0;
	const struct stats_by_string_key *entry;
	const struct stats_by_string_key **sorted, **sorted_entry;

	assert(count_p);

	for (entry = list; entry; entry = entry->next) {
		count++;
	}

	sorted = CALLOC(count, const struct stats_by_string_key *);
	sorted_entry = sorted;
	for (entry = list; entry; entry = entry->next) {
		*(sorted_entry++) = entry;
	}

	qsort(sorted, count, sizeof(struct stats_by_string_key *),
		stattest_stats_by_string_key_sort_compare);

	*count_p = count;
	return sorted;
}

enum key_class {
	key_all,
	key_faulty,
	key_golden,
	key_other,
};

static enum key_class key_classify(const char *key) {
	if (strcmp(key, "<ALL>") == 0) return key_all;
	if (strcmp(key, "<FAULTY>") == 0) return key_faulty;
	if (strcmp(key, "<GOLDEN>") == 0) return key_golden;
	return key_other;
}

enum set_comparison {
	set_invalid,
	set_disjoint,
	set_super1,
	set_super2,
};

static enum set_comparison output_analysis_stattest_set_compare(
	const char *key1, const struct stats_aggregate_per_test *stats1,
	const char *key2, const struct stats_aggregate_per_test *stats2) {
	int disjoint = 0;
	enum key_class keyclass1, keyclass2;
	int super1 = 0, super2 = 0;

	assert(key1);
	assert(stats1);
	assert(key2);
	assert(stats2);

	/* ALL is a superset of all other groups and FAULTY a superset of all
	 * proper keys; any other combinations are considered disjoint (note
	 * that GOLDEN may overlap with others in reality but this is not and
	 * issue as it has no valid cases)
	 */
	keyclass1 = key_classify(key1);
	keyclass2 = key_classify(key2);
	if (keyclass1 == key_all && keyclass1 != key_all) {
		super1 = 1;
	} else if (keyclass1 != key_all && keyclass2 == key_all) {
		super2 = 1;
	} else if (keyclass1 == key_faulty && keyclass2 == key_other) {
		super1 = 1;
	} else if (keyclass1 == key_other && keyclass2 == key_faulty) {
		super2 = 1;
	} else if (strcmp(key1, key2) != 0) {
		return set_invalid;
	}

	/* NULL means all */
	if (!stats1->system && stats2->system) {
		super1 = 1;
	} else if (stats1->system && !stats2->system) {
		super2 = 1;
	} else if (safestrcmp(stats1->system, stats2->system) != 0) {
		disjoint = 1;
	}

	/* NULL means all */
	if (!stats1->workload && stats2->workload) {
		super1 = 1;
	} else if (stats1->workload && !stats2->workload) {
		super2 = 1;
	} else if (safestrcmp(stats1->workload, stats2->workload) != 0) {
		disjoint = 1;
	}

	/* summarize the results */
	if (super1 && !super2 && !disjoint) return set_super1;
	if (!super1 && super2 && !disjoint) return set_super2;
	if (!super1 && !super2 && disjoint) return set_disjoint;
	fprintf(stderr, "warning: invalid comparison (%s, %s, %s) with "
		"(%s, %s, %s)\n", key1, stats1->system ? : "<ALL>",
		stats1->workload ? : "<ALL>", key2,
		stats2->system ? : "<ALL>", stats2->workload ? : "<ALL>");
	return set_invalid;
}

static double sqr(double x) {
	return x * x;
}

static void output_analysis_summary_write_chi2(const char *type,
	const char *name, const char *key1,
	const struct stats_aggregate_per_test *stats1, const char *key2,
	const struct stats_aggregate_per_test *stats2, double p,
	int value1, int total1, int value2, int total2);

static void print_chi2(FILE *file, const char *type, const char *name,
	const char *key1, const struct stats_aggregate_per_test *stats1,
	const char *key2, const struct stats_aggregate_per_test *stats2,
	enum set_comparison set_comparison, int value1, int total1,
	int value2, int total2) {
	double chi2;
	double expo1, expv1, expo2, expv2;
	int other1, other2;
	double othert, total, valuet;
	double p;

	assert(file);
	assert(value1 >= 0);
	assert(value1 <= total1);
	assert(value2 >= 0);
	assert(value2 <= total2);

	switch (set_comparison) {
	case set_disjoint: break;
	case set_invalid: goto invalid;
	case set_super1:
		assert(value1 >= value2);
		assert(total1 >= total2);
		value1 -= value2;
		total1 -= total2;
		break;
	case set_super2:
		assert(value2 >= value1);
		assert(total2 >= total1);
		value2 -= value1;
		total2 -= total1;
		break;
	}
	assert(value1 <= total1);
	assert(value2 <= total2);
	if (total1 <= 0 && total2 <= 0) goto invalid;

	other1 = total1 - value1;
	other2 = total2 - value2;
	othert = other1 + other2;
	valuet = value1 + value2;
	total = total1 + total2;
	expo1 = othert * total1 / total;
	expo2 = othert * total2 / total;
	expv1 = valuet * total1 / total;
	expv2 = valuet * total2 / total;
	if (expo1 < CHI2_EXPECTED_MIN || expo2 < CHI2_EXPECTED_MIN ||
		expv1 < CHI2_EXPECTED_MIN || expv2 < CHI2_EXPECTED_MIN) {
		goto invalid;
	}

	chi2 = sqr(other1 - expo1) / expo1 +
		sqr(other2 - expo2) / expo2 +
		sqr(value1 - expv1) / expv1 +
		sqr(value2 - expv2) / expv2;
	p = chi2_1df_p(chi2);
	fprintf(file, "%.3f\t", chi2);
	fprintf(file, "%g\t", p);

	output_analysis_summary_write_chi2(type, name, key1, stats1,
		key2, stats2, p, value1, total1, value2, total2);
	return;

invalid:
	fprintf(file, "\t");
	fprintf(file, "\t");
}

static void write_without_tabs(FILE *file, const char *s) {
	char c;

	while ((c = *(s++))) {
		if (c == '\t') c = '/';
		fputc(c, file);
	}
}

static void output_analysis_summary_write_comparison_part(
	const char *name, const char *key1,
	const struct stats_aggregate_per_test *stats1, const char *key2,
	const struct stats_aggregate_per_test *stats2, int *plural) {

	*plural = 1;
	if (strcmp(key1, key2) != 0) {
		switch (key_classify(key1)) {
		case key_all: fprintf(output_stattest_summary, "other %ss ", name); break;
		case key_faulty: fprintf(output_stattest_summary, "other faulty runs "); break;
		case key_golden: fprintf(output_stattest_summary, "golden runs "); break;
		case key_other:
			fprintf(output_stattest_summary, "%s ", name);
			write_without_tabs(output_stattest_summary, key1);
			fprintf(output_stattest_summary, " ");
			*plural = 0;
			break;
		}
	}
	if (safestrcmp(stats1->system, stats2->system) != 0) {
		if (stats1->system) {
			fprintf(output_stattest_summary, "%s ", stats1->system);
			*plural = 0;
		} else {
			fprintf(output_stattest_summary, "other systems ");
		}
	}
	if (safestrcmp(stats1->workload, stats2->workload) != 0) {
		if (stats1->workload) {
			fprintf(output_stattest_summary, "workload %s ", stats1->workload);
			*plural = 0;
		} else {
			fprintf(output_stattest_summary, "other workloads ");
		}
	}
}

static void swapstr(const char **x, const char **y) {
	const char *tmp;

	assert(x);
	assert(y);

	tmp = *x;
	*x = *y;
	*y = tmp;
}

static void swapstats(const struct stats_aggregate_per_test **x,
	const struct stats_aggregate_per_test **y) {
	const struct stats_aggregate_per_test *tmp;

	assert(x);
	assert(y);

	tmp = *x;
	*x = *y;
	*y = tmp;
}

static void swapint(int *x, int *y) {
	int tmp;

	assert(x);
	assert(y);

	tmp = *x;
	*x = *y;
	*y = tmp;
}

static void swapdouble(double *x, double *y) {
	double tmp;

	assert(x);
	assert(y);

	tmp = *x;
	*x = *y;
	*y = tmp;
}

static int output_analysis_summary_should_swap(
	const char *key1, const struct stats_aggregate_per_test *stats1,
	const char *key2, const struct stats_aggregate_per_test *stats2) {
	int first1, first2;

	assert(key1);
	assert(stats1);
	assert(key2);
	assert(stats2);

	first1 = (key_classify(key1) != key_all) +
		(key_classify(key1) == key_other) +
		(stats1->system != NULL) +
		(stats1->workload != NULL);
	first2 = (key_classify(key2) != key_all) +
		(key_classify(key2) == key_other) +
		(stats2->system != NULL) +
		(stats2->workload != NULL);
	return first1 < first2;
}

static void output_analysis_summary_write_comparison(const char *type,
	const char *name, const char *key1,
	const struct stats_aggregate_per_test *stats1, const char *key2,
	const struct stats_aggregate_per_test *stats2, double p,
	int n1, int n2, int is_more) {
	int needcomma = 0;
	int plural;

	if (option_test_name) fprintf(output_stattest_summary, "%s\t", option_test_name);
	fprintf(output_stattest_summary, "%s\t", stats1->system ? : "<ALL>");
	fprintf(output_stattest_summary, "%s\t", stats2->system ? : "<ALL>");
	fprintf(output_stattest_summary, "%s\t", stats1->workload ? : "<ALL>");
	fprintf(output_stattest_summary, "%s\t", stats2->workload ? : "<ALL>");
	fprintf(output_stattest_summary, "%s\t", name);
	fprintf(output_stattest_summary, "%s\t", type);
	fprintf(output_stattest_summary, "%d\t", n1);
	fprintf(output_stattest_summary, "%d\t", n2);
	fprintf(output_stattest_summary, "%g\t", p);

	if (strcmp(key1, key2) == 0) {
		switch (key_classify(key1)) {
		case key_all: break;
		case key_faulty: fprintf(output_stattest_summary, "for faulty runs"); needcomma = 1; break;
		case key_golden: fprintf(output_stattest_summary, "for golden runs"); needcomma = 1; break;
		case key_other:
			fprintf(output_stattest_summary, "for %s ", name);
			write_without_tabs(output_stattest_summary, key1);
			needcomma = 1;
			break;
		}
	}
	if (safestrcmp(stats1->system, stats2->system) == 0 && stats1->system) {
		if (needcomma) fprintf(output_stattest_summary, " ");
		fprintf(output_stattest_summary, "on %s", stats1->system);
		needcomma = 1;
	}
	if (safestrcmp(stats1->workload, stats2->workload) == 0 && stats1->workload) {
		if (needcomma) fprintf(output_stattest_summary, " ");
		fprintf(output_stattest_summary, "with workload %s", stats1->workload);
		needcomma = 1;
	}
	if (needcomma) fprintf(output_stattest_summary, ", ");
	output_analysis_summary_write_comparison_part(name,
		key1, stats1, key2, stats2, &plural);
	fprintf(output_stattest_summary, "%s %s likely to %s than ",
		plural ? "are" : "is", is_more ? "more" : "less", type);
	output_analysis_summary_write_comparison_part(name,
		key2, stats2, key1, stats1, &plural);
}

static void output_analysis_summary_write_significance(double p) {
	int digits;

	if (p < 0.00001) {
		fprintf(output_stattest_summary, "p < 0.00001");
	} else {
		digits = (int) ceil(-log10(p)) + 2;
		if (digits < 1) digits = 1;
		if (digits > 5) digits = 5;
		fprintf(output_stattest_summary, "p = %.*f", digits, p);
	}
}

static void output_analysis_summary_write_chi2(const char *type,
	const char *name, const char *key1,
	const struct stats_aggregate_per_test *stats1, const char *key2,
	const struct stats_aggregate_per_test *stats2, double p,
	int value1, int total1, int value2, int total2) {

	assert(key1);
	assert(stats1);
	assert(key2);
	assert(stats2);
	assert(value1 >= 0);
	assert(value2 >= 0);
	assert(value1 <= total1);
	assert(value2 <= total2);

	if (p > SIGNIFICANCE_TRESHOLD) return;

	assert(total1 > 0);
	assert(total2 > 0);

	if (output_analysis_summary_should_swap(key1, stats1, key2, stats2)) {
		swapstr(&key1, &key2);
		swapstats(&stats1, &stats2);
		swapint(&value1, &value2);
		swapint(&total1, &total2);
	}

	output_analysis_summary_write_comparison(type, name, key1, stats1, key2,
		stats2, p, total1, total2, value1 * total2 > value2 * total1);
	fprintf(output_stattest_summary, "(%.2f%% vs. %.2f%%; p = ",
		value1 * 100.0 / total1, value2 * 100.0 / total2);
	output_analysis_summary_write_significance(p);
	fprintf(output_stattest_summary, ")\n");
}

static void output_analysis_summary_write_t(const char *type,
	const char *name,
	const char *key1, const struct stats_aggregate_per_test *stats1,
	const char *key2, const struct stats_aggregate_per_test *stats2,
	double p, int n1, int n2, double diff1, double diff2) {

	assert(key1);
	assert(stats1);
	assert(key2);
	assert(stats2);
	assert(n1 >= 0);
	assert(n2 >= 0);

	if (p > SIGNIFICANCE_TRESHOLD) return;

	assert(n1 > 0);
	assert(n2 > 0);

	if (output_analysis_summary_should_swap(key1, stats1, key2, stats2)) {
		swapstr(&key1, &key2);
		swapstats(&stats1, &stats2);
		swapint(&n1, &n2);
		swapdouble(&diff1, &diff2);
	}

	output_analysis_summary_write_comparison(type, name, key1, stats1, key2,
		stats2, p, n1, n2, diff1 > diff2);
	fprintf(output_stattest_summary, "(%.1f%% vs. %.1f%%; p = ",
		diff1, diff2);
	output_analysis_summary_write_significance(p);
	fprintf(output_stattest_summary, ")\n");
}

static void output_analysis_stattest_write_time_diff(
	FILE *file,
	const char *name,
	const char *key1, const struct stats_aggregate_per_test *stats1,
	const char *key2, const struct stats_aggregate_per_test *stats2,
	enum set_comparison set_comparison,
	enum logstep step1,
	enum logstep step2) {
	double avg1, avg2;
	double df;
	struct stat_aggregate_double diff1, diff2;
	const struct stat_aggregate_double *difftotal1, *difftotal2;
	double p;
	double s, s2;
	double t;
	char type[64];
	double var1, var2;

	assert(file);
	assert((stats1 && stats2) || (!stats1 && !stats2));
	assert((stats1 && key1) || (!stats1 && !key1));
	assert((stats2 && key2) || (!stats2 && !key2));
	assert(step1 < step2);

	if (!stats1 && !stats2) {
		fprintf(file,
			"time_%1$s_to_%2$s_t\t"
			"time_%1$s_to_%2$s_df\t"
			"time_%1$s_to_%2$s_p\t"
			"time_%1$s_to_%2$s_n1\t"
			"time_%1$s_to_%2$s_avg1\t"
			"time_%1$s_to_%2$s_stdev1\t"
			"time_%1$s_to_%2$s_n2\t"
			"time_%1$s_to_%2$s_avg2\t"
			"time_%1$s_to_%2$s_stdev2\t",
			logstepname(step1), logstepname(step2));
		return;
	}

	difftotal1 = &stats1->stats.step_time_diff[step1][step2];
	difftotal2 = &stats2->stats.step_time_diff[step1][step2];
	diff1 = *difftotal1;
	diff2 = *difftotal2;
	switch (set_comparison) {
	case set_disjoint:
		break;
	case set_invalid:
		goto notest;
	case set_super1:
		assert(diff1.n >= diff2.n);
		assert(diff1.sum2 >= diff2.sum2);
		diff1.n -= diff2.n;
		diff1.sum -= diff2.sum;
		diff1.sum2 -= diff2.sum2;
		break;
	case set_super2:
		assert(diff2.n >= diff1.n);
		assert(diff2.sum2 >= diff1.sum2);
		diff2.n -= diff1.n;
		diff2.sum -= diff1.sum;
		diff2.sum2 -= diff1.sum2;
		break;
	}

	/* http://en.wikipedia.org/wiki/Student%27s_t-test#Equal_or_Unequal_sample_sizes.2C_unequal_variances */
	if (diff1.n <= 1 || diff2.n <= 1) goto notest;
	var1 = stat_aggregate_double_var(&diff1);
	var2 = stat_aggregate_double_var(&diff2);
	if (var1 <= 0 && var2 <= 0) goto notest;

	avg1 = stat_aggregate_double_avg(&diff1);
	avg2 = stat_aggregate_double_avg(&diff2);
	s2 = var1 / diff1.n + var2 / diff2.n;
	s = sqrt(s2);
	t = (avg1 - avg2) / s;
	df = sqr(s2) / (sqr(var1 / diff1.n) / (diff1.n - 1) +
		sqr(var2 / diff2.n) / (diff2.n - 1));
	if (df < 1) goto notest;

	p = student_t_2t_p(t, df);
	fprintf(file, "%g\t", t);
	fprintf(file, "%g\t", df);
	fprintf(file, "%g\t", p);

	snprintf(type, sizeof(type), "time_%s_to_%s",
		logstepname(step1), logstepname(step2));
	output_analysis_summary_write_t(type, name, key1, stats1,
		key2, stats2, p, diff1.n, diff2.n, avg1, avg2);
	goto otherfields;

notest:
	fprintf(file, "\t");
	fprintf(file, "\t");
	fprintf(file, "\t");

otherfields:
	fprintf(file, "%d\t", diff1.n);
	print_double_field(file, stat_aggregate_double_avg(&diff1), 6);
	print_double_field(file, stat_aggregate_double_stdev(&diff1), 6);
	fprintf(file, "%d\t", diff2.n);
	print_double_field(file, stat_aggregate_double_avg(&diff2), 6);
	print_double_field(file, stat_aggregate_double_stdev(&diff2), 6);
}

static void output_analysis_stattest_write_time_diffs(FILE *file,
	const char *name,
	const char *key1, const struct stats_aggregate_per_test *stats1,
	const char *key2, const struct stats_aggregate_per_test *stats2,
	enum set_comparison set_comparison) {

	assert(file);
	assert((stats1 && stats2) || (!stats1 && !stats2));

	output_analysis_stattest_write_time_diff(file, name, key1, stats1, key2, stats2, set_comparison, logstep_qemu_start, logstep_before_sanity_pre);
	output_analysis_stattest_write_time_diff(file, name, key1, stats1, key2, stats2, set_comparison, logstep_before_sanity_pre, logstep_after_sanity_pre);
	output_analysis_stattest_write_time_diff(file, name, key1, stats1, key2, stats2, set_comparison, logstep_after_sanity_pre, logstep_workload_end);
	output_analysis_stattest_write_time_diff(file, name, key1, stats1, key2, stats2, set_comparison, logstep_workload_end, logstep_after_sanity_post);
	output_analysis_stattest_write_time_diff(file, name, key1, stats1, key2, stats2, set_comparison, logstep_qemu_start, logstep_after_sanity_post);
}

static void output_analysis_stattest_write_pct(
	FILE *file, int value, int total) {
	if (total > 0) {
		print_double_field(file, value * 100.0 / total, 2);
	} else {
		fprintf(file, "\t");
	}
}

static int outcometotal(const struct stats_aggregate *stats,
	enum outcome outcome) {
	switch (outcome) {
	case outcome_inactive: return stats->n - stats->activated;
	case outcome_invalid: return stats->activated - stats->valid;
	case outcome_success: return stats->valid - stats->crash;
	case outcome_crash: return stats->crash;
	default: break;
	}

	fprintf(stderr, "error: invalid outcome %d\n", outcome);
	abort();
	return 0;
}

static void output_analysis_stattest_write(
	FILE *file,
	const char *name,
	size_t colcount, 
	const char *key1,
	const struct stats_aggregate_per_test *stats1,
	const char *key2,
	const struct stats_aggregate_per_test *stats2,
	int with_depth) {
	enum outcome outcome;
	enum set_comparison set_comparison;
	enum logstep step;
	int stepreached1[outcome_count][logstep_count + 1];
	int stepreached2[outcome_count][logstep_count + 1];
	char type[256];

	assert(file);
	assert(key1);
	assert(stats1);
	assert(key2);
	assert(stats2);

	if (stats1->stats.n < 5 || stats2->stats.n < 5) return;

	set_comparison = output_analysis_stattest_set_compare(key1, stats1,
		key2, stats2);

	if (option_test_name) fprintf(file, "%s\t", option_test_name);
	fprintf(file, "%s\t", stats1->system ? : "<ALL>");
	fprintf(file, "%s\t", stats2->system ? : "<ALL>");
	fprintf(file, "%s\t", stats1->workload ? : "<ALL>");
	fprintf(file, "%s\t", stats2->workload ? : "<ALL>");
	if (with_depth) {
		fprintf(file, "%d\t", (int) countchar(key1, '/'));
		fprintf(file, "%d\t", (int) countchar(key2, '/'));
	}
	print_key_with_colcount(file, key1, colcount);
	print_key_with_colcount(file, key2, colcount);
	fprintf(file, "%d\t", stats1->stats.n);
	fprintf(file, "%d\t", stats2->stats.n);
	fprintf(file, "%d\t", stats1->stats.activated);
	fprintf(file, "%d\t", stats2->stats.activated);
	fprintf(file, "%d\t", stats1->stats.valid);
	fprintf(file, "%d\t", stats2->stats.valid);
	fprintf(file, "%d\t", stats1->stats.crash);
	fprintf(file, "%d\t", stats2->stats.crash);
	output_analysis_stattest_write_pct(file, stats1->stats.valid,
		stats1->stats.activated);
	output_analysis_stattest_write_pct(file, stats2->stats.valid,
		stats2->stats.activated);
	print_chi2(file, "pass the pre-test", name, key1, stats1, key2, stats2,
		set_comparison, stats1->stats.valid, stats1->stats.activated,
		stats2->stats.valid, stats2->stats.activated);
	output_analysis_stattest_write_pct(file, stats1->stats.crash,
		stats1->stats.valid);
	output_analysis_stattest_write_pct(file, stats2->stats.crash,
		stats2->stats.valid);
	print_chi2(file, "crash", name, key1, stats1, key2, stats2,
		set_comparison, stats1->stats.crash, stats1->stats.valid,
		stats2->stats.crash, stats2->stats.valid);
	output_analysis_stattest_write_time_diffs(file, name,
		key1, stats1, key2, stats2, set_comparison);

	memset(stepreached1, 0, sizeof(stepreached1));
	memset(stepreached2, 0, sizeof(stepreached2));
	step = logstep_count - 1;
	for (;;) {
		for (outcome = 0; outcome < outcome_count; outcome++) {
			stepreached1[outcome][step] =
				stepreached1[outcome][step + 1] +
				stats1->stats.step_reached[outcome][step];
			stepreached2[outcome][step] =
				stepreached2[outcome][step + 1] +
				stats2->stats.step_reached[outcome][step];
		}
		if (step == 0) break;
		step--;
	}
	for (outcome = 0; outcome < outcome_count; outcome++) {
		for (step = logstep_qemu_start; step < logstep_qemu_exit; step++) {
			fprintf(file, "%d\t", stepreached1[outcome][step]);
			fprintf(file, "%d\t", stepreached2[outcome][step]);
		}
		for (step = logstep_qemu_start; step < logstep_qemu_exit; step++) {
			output_analysis_stattest_write_pct(file,
				stepreached1[outcome][step],
				stats1->stats.activated);
			output_analysis_stattest_write_pct(file,
				stepreached2[outcome][step],
				stats2->stats.activated);
			snprintf(type, sizeof(type), "reach step %s with outcome %s",
				logstepname(step), outcomename(outcome));
			print_chi2(file, type, name, key1, stats1, key2, stats2,
				set_comparison,
				stepreached1[outcome][step],
				outcometotal(&stats1->stats, outcome),
				stepreached2[outcome][step],
				outcometotal(&stats2->stats, outcome));
		}
	}
	fprintf(file, "\n");
}

static void output_analysis_stattest_write_per_test_compare_tests(
	FILE *file,
	const char *name,
	size_t colcount,
	const struct stats_by_string_key *stats,
	int with_depth,
	int compare_cross_system) {
	size_t count, i, j;
	struct stats_aggregate_per_test **sorted;

	assert(stats);

	if (!stats->stats_list) return;

	sorted = stattest_stats_aggregate_per_test_sort(stats->stats_list, &count);
	for (i = 0; i < count; i++) {
		for (j = i + 1; j < count; j++) {
			if (safestrcmp(sorted[i]->system, sorted[j]->system) != 0 &&
				safestrcmp(sorted[i]->workload, sorted[j]->workload) != 0) {
				continue;
			}
			if (sorted[i]->workload && sorted[j]->workload &&
				strcmp(sorted[i]->workload, sorted[j]->workload) != 0) {
				continue;
			}
			if (!compare_cross_system && 
				(!sorted[i]->system ||
				!sorted[j]->system ||
				strcmp(sorted[i]->system, sorted[j]->system) != 0)) {
				continue;
			}
			output_analysis_stattest_write(file, name, colcount,
				stats->key, sorted[i], stats->key,
				sorted[j], with_depth);
		}
	}
	FREE(sorted);
}

static void output_analysis_stattest_write_per_test_compare_keys(
	FILE *file,
	const char *name,
	size_t colcount,
	const struct stats_by_string_key *stats1,
	const struct stats_by_string_key *stats2,
	int with_depth,
	int compare_cross_system) {
	size_t count1, count2, i, j;
	struct stats_aggregate_per_test **sorted1, **sorted2;

	assert(stats1);
	assert(stats2);

	if (!stats1->stats_list) return;
	if (!stats2->stats_list) return;

	sorted1 = stattest_stats_aggregate_per_test_sort(stats1->stats_list, &count1);
	sorted2 = stattest_stats_aggregate_per_test_sort(stats2->stats_list, &count2);
	for (i = 0; i < count1; i++) {
		for (j = 0; j < count2; j++) {
			if (safestrcmp(sorted1[i]->system, sorted2[j]->system) != 0) continue;
			if (safestrcmp(sorted1[i]->workload, sorted2[j]->workload) != 0) continue;
			if (!compare_cross_system && !sorted1[i]->system) continue;
			output_analysis_stattest_write(file, name, colcount,
				stats1->key, sorted1[i], stats2->key,
				sorted2[j], with_depth);
		}
	}
	FREE(sorted2);
	FREE(sorted1);
}

static void output_analysis_stattest_write_header(
	FILE *file,
	const char *name,
	const char **cols,
	size_t *colcount,
	int with_depth) {
	const char **col;
	enum outcome outcome;
	enum logstep step;

	assert(file);
	assert(name);
	assert(cols);
	assert(colcount);

	if (option_test_name) fprintf(file, "testname\t");
	fprintf(file,
		"system1\t"
		"system2\t"
		"workload1\t"
		"workload2\t");
	if (with_depth) {
		fprintf(file, "%s_depth1\t", name);
		fprintf(file, "%s_depth2\t", name);
	}
	*colcount = 0;
	for (col = cols; *col; col++) {
		fprintf(file, "%s1\t", *col);
		(*colcount)++;
	}
	for (col = cols; *col; col++) {
		fprintf(file, "%s2\t", *col);
	}
	fprintf(file,
		"n1\t"
		"n2\t"
		"activated_n1\t"
		"activated_n2\t"
		"valid_n1\t"
		"valid_n2\t"
		"crash_n1\t"
		"crash_n2\t"
		"valid_pct1\t"
		"valid_pct2\t"
		"valid_chi2_1df\t"
		"valid_p\t"
		"crash_pct1\t"
		"crash_pct2\t"
		"crash_chi2_1df\t"
		"crash_p\t");
	output_analysis_stattest_write_time_diffs(file,
		NULL, NULL, NULL, NULL, NULL, 0);
	for (outcome = 0; outcome < outcome_count; outcome++) {
		for (step = 0; step < logstep_qemu_exit; step++) {
			fprintf(file, "reached_%s_%s_n1\t",
				outcomename(outcome), logstepdesc(step));
			fprintf(file, "reached_%s_%s_n2\t",
				outcomename(outcome), logstepdesc(step));
		}
		for (step = logstep_qemu_start; step < logstep_qemu_exit; step++) {
			fprintf(file, "reached_%s_%s_pct1\t",
				outcomename(outcome), logstepdesc(step));
			fprintf(file, "reached_%s_%s_pct2\t",
				outcomename(outcome), logstepdesc(step));
			fprintf(file, "reached_%s_%s_chi2_1df\t",
				outcomename(outcome), logstepdesc(step));
			fprintf(file, "reached_%s_%s_p\t",
				outcomename(outcome), logstepdesc(step));
		}
	}
	fprintf(file, "\n");
}

static void output_analysis_stattest_write_keys_cols(
	const char *name,
	const char **cols,
	const struct stats_by_string_key *list,
	int with_depth,
	int compare_cross_system) {
	size_t colcount;
	size_t count, i, j;
	FILE *file;
	char *path;
	const struct stats_by_string_key **sorted;

	assert(cols);

	if (!list) return;

	path = ASPRINTF("%s-%s-stattest.txt", option_output_prefix, name);
	file = fopen(path, "w");
	if (!file) {
		fprintf(stderr, "error: cannot open output file %s: %s\n",
			path, strerror(errno));
		exit(-1);
	}
	FREE(path);

	output_analysis_stattest_write_header(file, name, cols, &colcount,
		with_depth);

	sorted = stattest_stats_by_string_key_sort(list, &count);
	for (i = 0; i < count; i++) {
		output_analysis_stattest_write_per_test_compare_tests(
			file, name, colcount, sorted[i], with_depth,
			compare_cross_system);
	}
	for (i = 0; i < count; i++) {
		for (j = i + 1; j < count; j++) {
			if (key_classify(sorted[i]->key) == key_other &&
				key_classify(sorted[j]->key) == key_other) {
				continue;
			}
			output_analysis_stattest_write_per_test_compare_keys(
				file, name, colcount, sorted[i], sorted[j],
				with_depth, compare_cross_system);
		}
	}
	FREE(sorted);

	fclose(file);
}

static void output_analysis_stattest_write_keys(
	const char *name,
	const struct stats_by_string_key *list,
	int with_depth,
	int compare_cross_system) {
	const char *cols[] = { name, NULL };

	output_analysis_stattest_write_keys_cols(name, cols, list, with_depth,
		compare_cross_system);
}

static void output_analysis_stattest_write_all(void) {
	const char *cols_step[] = { "fault_first", NULL };
	char *path;

	path = ASPRINTF("%s-stattest-summary.txt", option_output_prefix);
	output_stattest_summary = fopen(path, "w");
	if (!output_stattest_summary) {
		fprintf(stderr, "error: cannot open output file %s: %s\n",
			path, strerror(errno));
		exit(-1);
	}
	FREE(path);

	if (option_test_name) fprintf(output_stattest_summary, "testname\t");
	fprintf(output_stattest_summary,
		"system1\t"
		"system2\t"
		"workload1\t"
		"workload2\t"
		"independent\t"
		"dependent\t"
		"n1\t"
		"n2\t"
		"p\t"
		"description\n");

	output_analysis_stattest_write_keys("fault_act_count", stats_per_fault_act_count, 0, 1);
	output_analysis_stattest_write_keys("fault_type", stats_per_fault_type, 0, 1);
	output_analysis_stattest_write_keys("module", stats_per_module, 0, 0);
	output_analysis_stattest_write_keys("path", stats_per_path, 1, 0);
	output_analysis_stattest_write_keys("pathclass", stats_per_pathclass, 0, 0);
	output_analysis_stattest_write_keys("restart_count", stats_per_restart_count, 0, 0);
	output_analysis_stattest_write_keys_cols("step", cols_step, stats_per_step, 0, 1);

	fclose(output_stattest_summary);
}

static char *strchrlast(char *s, char c) {
	char *result = NULL;
	while (*s) {
		if (*s == c) result = s;
		s++;
	}
	return result;
}

static void output_analysis_aggregate_golden(const struct loginfo *loginfo) {

	assert(loginfo);

	output_analysis_aggregate_all(loginfo, "<ALL>");
	output_analysis_aggregate_all(loginfo, "<GOLDEN>");
}

static void output_analysis_aggregate(const struct loginfo *loginfo) {
	struct loginfo_module *module;
	char *faultpath = NULL, *faultpathend;
	int fault_act_count = 0;
	int faultline_mod;
	const char *faultfunc_mod;
	const char *faultmodule = NULL;
	int faultmodule_count = 0;
	const char *faultpath_mod;
	const char *faulttype = NULL, *faulttype_mod;
	enum logstep step;
	enum logstep step_fault_first = logstep_count;
	int restart_count = 0;

	assert(loginfo);

	output_analysis_aggregate_all(loginfo, "<ALL>");
	output_analysis_aggregate_all(loginfo, "<FAULTY>");

	for (module = loginfo->modules; module; module = module->next) {
		lookup_map_info(module, &faultpath_mod, &faultline_mod,
			&faultfunc_mod, &faulttype_mod);
		if (faultpath_mod && !faultpath) faultpath = STRDUP(faultpath_mod);
		if (faulttype_mod && !faulttype) faulttype = faulttype_mod;
		if (module->fault_bb_index_set) {
			if (!faultmodule) faultmodule = module->name;
			faultmodule_count++;
		}
		for (step = 0; step < logstep_count; step++) {
			if (module->fault_count[step] > 0) {
				if (step_fault_first > step) step_fault_first = step;
			}
			fault_act_count += module->fault_count[step];
			restart_count += module->restart_count[step];
		}
	}

	output_analysis_aggregate_fault_act_count(loginfo, NULL, fault_act_count);

	if (faulttype) output_analysis_aggregate_fault_type(loginfo, faulttype);
	
	if (faultmodule_count > 1) {
		output_analysis_aggregate_module(loginfo, "<MULTIPLE>");
	} else {
		output_analysis_aggregate_module(loginfo, faultmodule ? : "<NONE>");
	}

	if (faultpath && *faultpath) {
		output_analysis_aggregate_pathclass(loginfo,
			pathclassify(faultpath));
		for (;;) {
			output_analysis_aggregate_path(loginfo, faultpath);
			faultpathend = strchrlast(faultpath, '/');
			if (!faultpathend || faultpathend == faultpath) break;
			*faultpathend = 0;
		}
		FREE(faultpath);
	} else {
		output_analysis_aggregate_path(loginfo, "<UNKNOWN>");
		output_analysis_aggregate_pathclass(loginfo, "other");
	}

	output_analysis_aggregate_restart_count(loginfo, NULL, restart_count);

	output_analysis_aggregate_step(loginfo, NULL, step_fault_first);
}

static void output_analysis(const struct loginfo *loginfo) {
	assert(loginfo);

	output_analysis_all(loginfo);
	if (is_golden(loginfo)) {
		output_analysis_aggregate_add_per_test(loginfo,
			&stats_golden, 0);
	} else {
		output_analysis_aggregate(loginfo);
	}
}

static void output_write(void) {
	output_analysis_aggregate_golden_all();
	output_analysis_aggregate_write_all();
	output_analysis_stattest_write_all();
	if (output_all) {
		fclose(output_all);
		output_all = NULL;
	}
}

static void check_golden_run_consistency_module(
	const char *logpath,
	const struct loginfo_module *module,
	const struct loginfo_module *module_golden) {
#ifndef IGNORE_EXECCOUNTS
	int bb_extra, bb_missing;
#endif
	int restart = 0, restart_golden = 0;
	enum logstep step;

	for (step = 0; step < logstep_count; step++) {
		restart += module->restart_count[step];
		restart_golden += module_golden->restart_count[step];
	}
	if (restart != restart_golden) {
		fprintf(stderr, "warning: golden run %s restarts module %s %d "
			"times while earlier golden runs do so %d times\n",
			logpath, module->name, restart, restart_golden);
		return;
	}

#ifndef IGNORE_EXECCOUNTS
	if (module->bb_count != module_golden->bb_count) {
		fprintf(stderr, "warning: golden run %s match earlier golden "
			"runs in basic block count of module %s (%d vs. %d)\n",
			logpath, module->name, module->bb_count,
			module_golden->bb_count);
		return;
	}
	match_bbs_executed(module, module_golden, &bb_extra, &bb_missing, 1);
	if (bb_extra > 0) {
		fprintf(stderr, "warning: golden run %s executes %d basic "
			"block(s) in module %s not executed in earlier "
			"golden runs\n", logpath, bb_extra, module->name);
		return;
	}
	if (bb_missing > 0) {
		fprintf(stderr, "warning: golden run %s does not execute %d "
			"basic block(s) in module %s executed in earlier "
			"golden runs\n", logpath, bb_missing, module->name);
		return;
	}
#endif
}

static void check_golden_run_consistency(
	const struct loginfo *loginfo) {
	const struct loginfo *loginfo_golden;
	const struct loginfo_module *module;
	const struct loginfo_module *module_golden;

	assert(loginfo);

	if (!loginfo->sanity_passed_pre || !loginfo->sanity_passed_post) {
		fprintf(stderr, "warning: golden run %s does not pass "
			"sanity tests\n", loginfo->logpath);
	}

	loginfo_golden = golden_find(loginfo);
	if (!loginfo_golden) return;

	for (module_golden = loginfo_golden->modules; module_golden;
		module_golden = module_golden->next) {
		module = module_loginfo_find(loginfo_golden,
			module_golden->name);
		if (!module) {
			fprintf(stderr, "warning: golden run %s lacks module "
				"%s present in earlier golden run\n",
				loginfo->logpath, module_golden->name);
		}
	}

	for (module = loginfo->modules; module; module = module->next) {
		module_golden = module_loginfo_find(loginfo_golden,
			module->name);
		if (!module_golden) {
			fprintf(stderr, "warning: golden run %s has extra "
				"module %s not present in earlier golden run\n",
				loginfo->logpath, module->name);
			continue;
		}

		check_golden_run_consistency_module(loginfo->logpath, module,
			module_golden);
	}
}

static void process_log(const char *path) {
	struct loginfo loginfo;
	struct loginfo_list *node;

	assert(path);

	loginfo_load_from_log(path, &loginfo);
	output_analysis(&loginfo);

	if (!is_golden(&loginfo)) {
		/* fault specified -> faulty run */
		if (!golden_find(&loginfo)) {
			fprintf(stderr, "warning: faulty run %s specified "
				"not preceded by golden run, "
				"cannot compare\n", path);
		}
		loginfo_free(&loginfo);
		log_count++;
	} else {
		/* no fault specified -> golden run */
		if (log_count > 0) {
			fprintf(stderr, "warning: golden run %s specified "
				"after faulty run, comparison is incomplete\n",
				path);
		}
		check_golden_run_consistency(&loginfo);

		node = CALLOC(1, struct loginfo_list);
		node->data = loginfo;
		node->next = loginfo_golden_list;
		loginfo_golden_list = node;
	}
}

static void usage(const char *msg) {
	assert(msg);

	printf("%s\n", msg);
	printf("\n");
	printf("usage:\n");
	printf("  hypermemloganalyze [-o output-prefix] [-s source-path-prefix] [-t test-name] logpath-golden... logpath... mappath...\n");
	exit(1);
}

int main(int argc, char **argv) {
	const char *path;
	int index, r;

	/* handle options */
	while ((r = getopt(argc, argv, "o:s:t:")) >= 0) {
		switch (r) {
		case 'o':
			option_output_prefix = optarg;
			break;
		case 's':
			option_source_path_prefix = optarg;
			break;
		case 't':
			option_test_name = optarg;
			break;
		default:
			usage("Unknown flag specified");
			break;
		}
	}

	/* load map files */
	index = optind;
	while (index < argc) {
		path = argv[index++];
		if (ends_with(path, ".map", 1)) {
			load_map(path);
		}
	}
	if (!modules) usage("No map files specified");

	/* process log files */
	index = optind;
	while (index < argc) {
		path = argv[index++];
		if (!ends_with(path, ".map", 1)) {
			process_log(path);
		}
	}
	if (log_count < 1) fprintf(stderr, "warning: no faulty logs specified");

	/* write aggregate statistics and clean up */
	output_write();

	modules_free(modules);
	modules = NULL;
	return 0;
}
