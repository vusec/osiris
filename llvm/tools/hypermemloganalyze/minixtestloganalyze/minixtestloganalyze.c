#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "coverage.h"
#include "debug.h"
#include "helper.h"
#include "function.h"
#include "minixtestlogsummarize.h"
#include "minixteststats.h"
#include "module.h"
#include "pathlist.h"

static struct string_ll *excludemod;

static struct string_ll *excludetest;

static struct module_ll *modules;

static int setting_extra_stats;

static int setting_fewer_stats;

static int setting_max_count = INT_MAX;

static int setting_naive_mode;

static int setting_pretest_count;

static int setting_show_progress;

static struct minixtest_stats stats;

static int string_ll_contains(const struct string_ll *ll, const char *s) {
	while (ll) {
		if (strcmp(s, ll->str) == 0) return 1;
		ll = ll->next;
	}
	return 0;
}

static int ignoretest(const char *name) {
	return string_ll_contains(excludetest, name);
}

static const char *outcome_to_str(enum run_outcome outcome) {
	switch (outcome) {
	case ro_success: return "success";
	case ro_failed: return "failed";
	case ro_shutdown: return "shutdwn";
	case ro_crash: return "crash";
	case ro_timeout: return "timeout";
	case run_outcome_count: return "total";
	}
	return "???";
}

static const char *ltckpt_method_to_str(enum ltckpt_method method) {
	switch (method) {
	case lm_generic: return "generic";
	case lm_deadlock: return "deadlock";
	case lm_failstop: return "failstop";
	case lm_idempotent: return "idempotent";
	case lm_process_specific: return "procspec";
	case lm_service_fi: return "service_fi";
	case lm_writelog: return "writelog";
	case ltckpt_method_count: return "total";
	}
	return "?";
}

static const char *ltckpt_status_to_str(enum ltckpt_status status) {
	switch (status) {
	case ls_failure: return "failure";
	case ls_shutdown: return "shutdown";
	case ls_start: return "start";
	case ls_success: return "success";
	case ltckpt_status_count: return "total";
	}
	return "?";
}

static int sum_recovery(
	const struct minixtest_log_summary_recoveries *recoveries,
	enum ltckpt_method method,
	enum ltckpt_status status) {
	enum ltckpt_method m, m1, m2;
	enum ltckpt_status s, s1, s2;
	int total = 0;

	if (method < ltckpt_method_count) {
		m1 = m2 = method;
	} else {
		m1 = 0;
		m2 = ltckpt_method_count - 1;
	}
	if (status < ltckpt_status_count) {
		s1 = s2 = status;
	} else {
		s1 = 0;
		s2 = ltckpt_status_count - 1;
	}
	for (m = m1; m <= m2; m++) {
	for (s = s1; s <= s2; s++) {
		total += recoveries->count[s][m];
	}
	}
	return total;
}

static double timestamp_to_float(const struct timeval *t) {
	return t->tv_sec + t->tv_usec / 1000000.0;
}

static int show_recovery(enum ltckpt_method method, enum ltckpt_status status) {
	/* must match combinations sent to print_ltckpt in logparse.c */
	switch (status) {
	case ls_failure:
		switch (method) {
		case lm_generic: return 1;
		case lm_deadlock: return 0; /* cannot fail */
		case lm_failstop: return 0; /* cannot fail */
		case lm_idempotent: return 1;
		case lm_process_specific: return 1;
		case lm_service_fi: return 0; /* cannot fail */
		case lm_writelog: return 1;
		case ltckpt_method_count: return 1; /* total */
		}
		break;
	case ls_shutdown:
		switch (method) {
		case lm_generic: return 1;
		case lm_deadlock: return 1;
		case lm_failstop: return 1;
		case lm_idempotent: return 0; /* does not shutdown */
		case lm_process_specific: return 0; /* does not shutdown */
		case lm_service_fi: return 0; /* does not shutdown */
		case lm_writelog: return 0; /* does not shutdown */
		case ltckpt_method_count: return 1; /* total */
		}
		break;
	case ls_start:
		switch (method) {
		case lm_generic: return 1;
		case lm_deadlock: return 0; /* recovery not started */
		case lm_failstop: return 0; /* follows generic start */
		case lm_idempotent: return 0; /* follows generic start */
		case lm_process_specific: return 0; /* follows generic start */
		case lm_service_fi: return 0; /* follows generic start */
		case lm_writelog: return 1;
		case ltckpt_method_count: return 1; /* total */
		}
		return method == lm_generic || method == ltckpt_method_count;
	case ls_success:
		switch (method) {
		case lm_generic: return 0; /* not applicable */
		case lm_deadlock: return 0; /* cannot succeed */
		case lm_failstop: return 0; /* cannot succeed */
		case lm_idempotent: return 1;
		case lm_process_specific: return 1;
		case lm_service_fi: return 1;
		case lm_writelog: return 1;
		case ltckpt_method_count: return 1; /* total */
		}
		break;
	case ltckpt_status_count:
		return 1; /* total */
	}
	assert(0 || "Did you not see the compiler warning for the missing case?");
	return 1;
}

static void dump_stats_for_run(
	FILE *file,
	const struct minixtest_log_summary *summary,
	const struct run_classification *classification) {
	static int first = 1;
	execcount faultact = 0;
	enum ltckpt_method method;
	const struct minixtest_log_summary_module *module;
	int modcount = 0;
	int modcountfaultact = 0;
	int modcountfaultinj = 0;
	int modcountfaultretr = 0;
	struct minixtest_log_summary_recoveries recoveries = { };
	enum ltckpt_status status;
	const struct minixtest_log_summary_test *test;
	int testcountcrashed = 0;
	int testcountexecuted = 0;
	int testcountfailed = 0;
	int testcountpassed = 0;
	int testcountplanned;

	if (first) {
		fprintf(file,
			"logpath\t"
			"faultspec\t"
			"testspec\t"
			"outcome\t"
			"outcome-pretest\t"
			"outcome-recovery\t"
			"status-final\t"
			"exited\t"
			"modcount\t"
			"modcountfaultretr\t"
			"modcountfaultinj\t"
			"modcountfaultact\t"
			"faultact\t"
			"faultactstatusfirst\t"
			"faultactstatuslast\t");
		for (status = 0; status <= ltckpt_status_count; status++) {
		for (method = 0; method <= ltckpt_method_count; method++) {
			if (!show_recovery(method, status)) continue;
			fprintf(file, "recovercount-%s-%s\t",
				ltckpt_method_to_str(method),
				ltckpt_status_to_str(status));
		}
		}
		fprintf(file,
			"timestart\t"
			"timeboot\t"
			"timetestsstart\t"
			"timetestcomplete\t"
			"timeexit\t"
			"testcountexecuted\t"
			"testcountplanned\t"
			"testcountpassed\t"
			"testcountfailed\t"
			"testcountcrashed\n");
		first = 0;
	}

	for (module = summary->modules; module; module = module->next) {
		modcount++;
		if (module->faultindexretrieved > 0) {
			modcountfaultretr++;
			if (module->faultindex >= 0) modcountfaultinj++;
		}
		if (module->faultact > 0) {
			modcountfaultact++;
			faultact += module->faultact;
		}
		recoveries_add(&recoveries, &module->recoveries, 1);
	}
	for (test = summary->tests; test; test = test->next) {
		if (ignoretest(test->name)) continue;
		testcountexecuted++;
		switch (test->status) {
		case mts_running: testcountcrashed++; break;
		case mts_passed: testcountpassed++; break;
		case mts_failed: testcountfailed++; break;
		}
	}
	testcountplanned = wordcount(summary->testsplanned);
	fprintf(file,
		"%s\t"
		"%s\t"
		"%s\t"
		"%s\t"
		"%d\t"
		"%d\t"
		"%s\t"
		"%d\t"
		"%d\t"
		"%d\t"
		"%d\t"
		"%d\t"
		"%lld\t"
		"%s\t"
		"%s\t",
		summary->logpath,
		summary->faultspec ? : "",
		summary->testsplanned ? : "",
		outcome_to_str(classification->outcome),
		classification->pretest,
		classification->recovery,
		minixtest_log_status_to_str(summary->status),
		summary->status_exited,
		modcount,
		modcountfaultretr,
		modcountfaultinj,
		modcountfaultact,
		(long long) faultact,
		(summary->statusfaultfirst == mtls_none) ? "" : minixtest_log_status_to_str(summary->statusfaultfirst),
		(summary->statusfaultlast == mtls_none) ? "" : minixtest_log_status_to_str(summary->statusfaultlast));
	for (status = 0; status <= ltckpt_status_count; status++) {
	for (method = 0; method <= ltckpt_method_count; method++) {
		if (!show_recovery(method, status)) continue;
		fprintf(file, "%d\t", sum_recovery(&recoveries, method, status));
	}
	}
	fprintf(file,
		"%.6g\t"
		"%.6g\t"
		"%.6g\t"
		"%.6g\t"
		"%.6g\t"
		"%d\t"
		"%d\t"
		"%d\t"
		"%d\t"
		"%d\n",
		timestamp_to_float(&summary->timestamp_start),
		timestamp_to_float(&summary->timestamp_boot),
		timestamp_to_float(&summary->timestamp_tests_starting),
		timestamp_to_float(&summary->timestamp_tests_complete),
		timestamp_to_float(&summary->timestamp_exit),
		testcountexecuted,
		testcountplanned,
		testcountpassed,
		testcountfailed,
		testcountcrashed);
}

static void load_log(const char *logpath, FILE *fileperrun) {
	struct run_classification classification;
	struct minixtest_log_summary summary;

	if (setting_max_count <= 0) {
		dbgprintf("skipping log file %s\n", logpath);
		return;
	}

	dbgprintf("loading log file %s\n", logpath);
	minixtest_log_summarize(logpath, excludemod, &summary);
	minixtest_stats_add(&stats, &classification, &summary,
		setting_pretest_count, setting_naive_mode);
	if (fileperrun) dump_stats_for_run(fileperrun, &summary, &classification);
	minixtest_log_summary_free(&summary);
	setting_max_count--;
}

static void load_map(const char *mappath) {
	dbgprintf("loading map file %s\n", mappath);
	module_load_from_map_ll(mappath, &modules);
}

static void dump_stats_for_test_get_coverage(
	const struct minixtest_stats_test *test,
	const char *modname,
	const struct pathlistentry *path,
	struct coverage_stats *covstats,
	struct coverage_stats *covstats_ex_boot) {
	const struct module *module;

	assert(test);
	assert(test->coverage.functions);
	assert(!modname || !path);
	
	if (modname) {
		module = module_find(modules, modname);
		if (!module) {
			fprintf(stderr, "warning: no map file "
				"for module %s\n", modname);
			return;
		}
		coverage_compute_module(&test->coverage, NULL, module, covstats);
		coverage_compute_module(&test->coverage, &stats.boot.coverage,
			module, covstats_ex_boot);
	} else {
		coverage_compute_total(
			&test->coverage,
			NULL,
			covstats,
			1,
			path ? path->path : NULL,
			path ? path->len : 0);
		coverage_compute_total(
			&test->coverage,
			&stats.boot.coverage,
			covstats_ex_boot,
			1,
			path ? path->path : NULL,
			path ? path->len : 0);
	}
}

static void printpct(FILE *file, int count, int total) {
	if (total > 0) {
		fprintf(file, "\t%g", 100.0 * count / total);
	} else {
		fprintf(file, "\t");
	}
}

static void printflt(FILE *file, double value) {
	if (!isnan(value)) {
		fprintf(file, "\t%g", value);
	} else {
		fprintf(file, "\t");
	}
}

static void printint(FILE *file, int value) {
	fprintf(file, "\t%d", value);
}

static void dump_stats_for_test_with_duration(
	FILE *file,
	struct minixtest_stats_test *test,
	const char *modname,
	const struct pathlistentry *path,
	int perfonly,
	int withbench,
	const char *name,
	double duration_mean,
	double duration_median,
	double duration_stdev,
	double duration_stderr) {
	struct coverage_stats covstats = { }, covstats_ex_boot = { };
	int perfstats = !perfonly || setting_extra_stats;

	assert(!modname || !path);
	assert(!modname || !perfonly);
	assert(!path || !perfonly);

	if (setting_show_progress && !modname && !path) {
		fprintf(stderr, "progress: test %s\n", name);
		fflush(stderr);
	}

	if (!perfonly && test) {
		dump_stats_for_test_get_coverage(
			test,
			modname,
			path,
			&covstats,
			&covstats_ex_boot);
	}

#define PRINTFLT(value) do { if (test) printflt(file, value); else fprintf(file, "\t"); } while (0)
#define PRINTINT(value) do { if (test) printint(file, value); else fprintf(file, "\t"); } while (0)
#define PRINTPCT(count, total) do { if (test) printpct(file, count, total); else fprintf(file, "\t"); } while (0)

	fprintf(file, "%s", name);
	if (!perfonly) {
		fprintf(file, "\t%s", modname ? : "<all>");
		fprintf(file, "\t%.*s", path ? (int) path->len : 5, path ? path->path : "<all>");
		PRINTINT(covstats.cov_func);
		PRINTINT(covstats.cov_bb);
		PRINTINT(covstats.cov_ins);
		PRINTINT(covstats.cov_fc);
		PRINTINT(covstats.cov_inj);
		PRINTINT(covstats.cov_loc);
		PRINTINT(covstats_ex_boot.cov_func);
		PRINTINT(covstats_ex_boot.cov_bb);
		PRINTINT(covstats_ex_boot.cov_ins);
		PRINTINT(covstats_ex_boot.cov_fc);
		PRINTINT(covstats_ex_boot.cov_inj);
		PRINTINT(covstats_ex_boot.cov_loc);
		PRINTINT(covstats.tot_func);
		PRINTINT(covstats.tot_bb);
		PRINTINT(covstats.tot_ins);
		PRINTINT(covstats.tot_fc);
		PRINTINT(covstats.tot_inj);
		PRINTINT(covstats.tot_loc);
		PRINTPCT(covstats.cov_func, covstats.tot_func);
		PRINTPCT(covstats.cov_bb, covstats.tot_bb);
		PRINTPCT(covstats.cov_ins, covstats.tot_ins);
		PRINTPCT(covstats.cov_fc, covstats.tot_fc);
		PRINTPCT(covstats.cov_inj, covstats.tot_inj);
		PRINTPCT(covstats.cov_loc, covstats.tot_loc);
		PRINTPCT(covstats_ex_boot.cov_func, covstats.tot_func);
		PRINTPCT(covstats_ex_boot.cov_bb, covstats.tot_bb);
		PRINTPCT(covstats_ex_boot.cov_ins, covstats.tot_ins);
		PRINTPCT(covstats_ex_boot.cov_fc, covstats.tot_fc);
		PRINTPCT(covstats_ex_boot.cov_inj, covstats.tot_inj);
		PRINTPCT(covstats_ex_boot.cov_loc, covstats.tot_loc);
	}
	if (!modname && !path) {
		printflt(file, duration_median);
		if (perfstats) printflt(file, duration_mean);
		if (!setting_fewer_stats) printflt(file, duration_stdev);
		if (perfstats) printflt(file, duration_stderr);
		if (perfstats) printflt(file, duration_mean / duration_stdev);
		if (perfstats) PRINTFLT(stats_min(&test->duration));
		if (perfstats) PRINTFLT(stats_max(&test->duration));
		if (perfstats) PRINTFLT(stats_max(&test->duration) - stats_min(&test->duration));
		if (perfstats) PRINTINT(test->duration.n);
		if (withbench) {
			PRINTFLT(stats_mean_unixbench(&test->reported_count));
			if (!setting_fewer_stats) PRINTFLT(stats_median(&test->reported_count));
			if (perfstats) PRINTFLT(stats_mean(&test->reported_count));
			if (!setting_fewer_stats) PRINTFLT(stats_stdev(&test->reported_count));
			if (!perfonly) PRINTFLT(stats_stderr(&test->reported_count));
			if (!perfonly) PRINTFLT(stats_min(&test->reported_count));
			if (!perfonly) PRINTFLT(stats_max(&test->reported_count));
			if (!perfonly) PRINTINT(test->reported_count.n);
			PRINTFLT(stats_mean_unixbench(&test->reported_rate));
			if (!setting_fewer_stats) PRINTFLT(stats_median(&test->reported_rate));
			if (perfstats) PRINTFLT(stats_mean(&test->reported_rate));
			if (!setting_fewer_stats) PRINTFLT(stats_stdev(&test->reported_rate));
			if (!perfonly) PRINTFLT(stats_stderr(&test->reported_rate));
			if (!perfonly) PRINTFLT(stats_min(&test->reported_rate));
			if (!perfonly) PRINTFLT(stats_max(&test->reported_rate));
			if (!perfonly) PRINTINT(test->reported_rate.n);
			PRINTFLT(stats_median(&test->reported_time));
			if (perfstats) PRINTFLT(stats_mean(&test->reported_time));
			if (!setting_fewer_stats) PRINTFLT(stats_stdev(&test->reported_time));
			if (!perfonly) PRINTFLT(stats_stderr(&test->reported_time));
			if (!perfonly) PRINTFLT(stats_min(&test->reported_time));
			if (!perfonly) PRINTFLT(stats_max(&test->reported_time));
			if (!perfonly) PRINTINT(test->reported_time.n);
		}
		if (!perfonly) PRINTINT(test->count_pass);
		if (!perfonly) PRINTINT(test->count_fail);
		if (!perfonly) PRINTINT(test->count_crash);
	}
	fprintf(file, "\n");

#undef PRINTFLT
#undef PRINTINT
#undef PRINTPCT
}

static void dump_stats_for_test(
	FILE *file,
	struct minixtest_stats_test *test,
	const char *modname,
	const struct pathlistentry *path,
	int perfonly,
	int withbench) {

	assert(test);

	dump_stats_for_test_with_duration(
		file,
		test,
		modname,
		path,
		perfonly,
		withbench,
		test->name,
		stats_mean(&test->duration),
		stats_median(&test->duration),
		stats_stdev(&test->duration),
		stats_stderr(&test->duration));
}

struct sumstats {
	double mean;
	double median;
	double var;
	double var_div_n;
};

static void sumstats_add(
	struct sumstats *sumstats,
	struct stats *stats) {

	assert(sumstats);
	assert(stats);

	sumstats->mean += stats_mean(stats);
	sumstats->median += stats_median(stats);
	sumstats->var += stats_var(stats);
	if (stats->n > 0) {
		sumstats->var_div_n += stats_var(stats) / stats->n;
	} else {
		sumstats->var_div_n = NAN;
	}
}

static void dump_stats_per_test_for_module(
	FILE *file,
	const char *modname,
	const struct pathlistentry *path,
	int perfonly,
	int withbench) {
	struct sumstats duration_sum = { };
	struct minixtest_stats_test *test;

	for (test = stats.tests; test; test = test->next) {
		if (ignoretest(test->name)) continue;
		dump_stats_for_test(file, test, modname, path, perfonly, withbench);
		sumstats_add(&duration_sum, &test->duration);
	}
	dump_stats_for_test(file, &stats.boot, modname, path, perfonly, withbench);
	dump_stats_for_test(file, &stats.between, modname, path, perfonly, withbench);
	dump_stats_for_test(file, &stats.alltests, modname, path, perfonly, withbench);
	dump_stats_for_test(file, &stats.total, modname, path, perfonly, withbench);

	sumstats_add(&duration_sum, &stats.boot.duration);
	sumstats_add(&duration_sum, &stats.between.duration);
	dump_stats_for_test_with_duration(
		file,
		NULL /* test */,
		modname,
		path,
		perfonly,
		withbench,
		"<sum>",
		duration_sum.mean,
		duration_sum.median,
		sqrt(duration_sum.var),
		sqrt(duration_sum.var_div_n));
}

static void dump_stats_per_test_per_module(FILE *file, int withbench) {
	const struct minixtest_stats_module *module;

	for (module = stats.total.modules; module; module = module->next) {
		if (setting_show_progress) {
			fprintf(stderr, "progress: module %s\n", module->name);
			fflush(stderr);
		}
		dump_stats_per_test_for_module(
			file,
			module->name,
			NULL /* path */,
			0 /* perfonly */,
			withbench);
	}
}

static void dump_stats_per_test_per_path(
	FILE *file,
	const struct pathlist *pathlist,
	int withbench) {
	const struct pathlistentry *entry;
	size_t i;

	assert(pathlist);
	assert(pathlist->entries || pathlist->count == 0);

	for (i = 0; i < pathlist->count; i++) {
		entry = &pathlist->entries[i];
		if (setting_show_progress) {
			fprintf(stderr, "progress: path %.*s\n",
				(int) entry->len, entry->path);
			fflush(stderr);
		}
		dump_stats_per_test_for_module(
			file,
			NULL /* modname */,
			entry,
			0 /* perfonly */,
			withbench);
	}
}

static int have_bench_results_test(const struct minixtest_stats_test *test) {
	assert(test);

	return test->reported_count.n > 0 ||
		test->reported_rate.n > 0 ||
		test->reported_time.n > 0;
}

static int have_bench_results_tests(const struct minixtest_stats_test *tests) {
	const struct minixtest_stats_test *test;

	for (test = tests; test; test = test->next) {
		if (ignoretest(test->name)) continue;
		if (have_bench_results_test(test)) return 1;
	}
	return 0;
}

static int have_bench_results(const struct minixtest_stats *stats) {

	assert(stats);

	return have_bench_results_tests(stats->tests) ||
		have_bench_results_test(&stats->boot) ||
		have_bench_results_test(&stats->between) ||
		have_bench_results_test(&stats->alltests) ||
		have_bench_results_test(&stats->total);
}

static void dump_stats_per_test_with_file(
	FILE *file,
	const struct pathlist *pathlist,
	int perfonly) {
	int perfstats = !perfonly || setting_extra_stats;
	int withbench;

	assert(file);
	assert(pathlist || perfonly);

	withbench = have_bench_results(&stats);

	fprintf(file, "test");
	if (!perfonly) fprintf(file, "\tmodule");
	if (!perfonly) fprintf(file, "\tpath");
	if (!perfonly) fprintf(file, "\tcov-func");
	if (!perfonly) fprintf(file, "\tcov-bb");
	if (!perfonly) fprintf(file, "\tcov-ins");
	if (!perfonly) fprintf(file, "\tcov-fc");
	if (!perfonly) fprintf(file, "\tcov-inj");
	if (!perfonly) fprintf(file, "\tcov-loc");
	if (!perfonly) fprintf(file, "\tcov-func-ex-boot");
	if (!perfonly) fprintf(file, "\tcov-bb-ex-boot");
	if (!perfonly) fprintf(file, "\tcov-ins-ex-boot");
	if (!perfonly) fprintf(file, "\tcov-fc-ex-boot");
	if (!perfonly) fprintf(file, "\tcov-inj-ex-boot");
	if (!perfonly) fprintf(file, "\tcov-loc-ex-boot");
	if (!perfonly) fprintf(file, "\ttot-func");
	if (!perfonly) fprintf(file, "\ttot-bb");
	if (!perfonly) fprintf(file, "\ttot-ins");
	if (!perfonly) fprintf(file, "\ttot-fc");
	if (!perfonly) fprintf(file, "\ttot-inj");
	if (!perfonly) fprintf(file, "\ttot-loc");
	if (!perfonly) fprintf(file, "\tcov-func-pct");
	if (!perfonly) fprintf(file, "\tcov-bb-pct");
	if (!perfonly) fprintf(file, "\tcov-ins-pct");
	if (!perfonly) fprintf(file, "\tcov-fc-pct");
	if (!perfonly) fprintf(file, "\tcov-inj-pct");
	if (!perfonly) fprintf(file, "\tcov-loc-pct");
	if (!perfonly) fprintf(file, "\tcov-func-ex-boot-pct");
	if (!perfonly) fprintf(file, "\tcov-bb-ex-boot-pct");
	if (!perfonly) fprintf(file, "\tcov-ins-ex-boot-pct");
	if (!perfonly) fprintf(file, "\tcov-fc-ex-boot-pct");
	if (!perfonly) fprintf(file, "\tcov-inj-ex-boot-pct");
	if (!perfonly) fprintf(file, "\tcov-loc-ex-boot-pct");
	fprintf(file, "\tduration-median");
	if (perfstats) fprintf(file, "\tduration-mean");
	if (!setting_fewer_stats) fprintf(file, "\tduration-stdev");
	if (perfstats) fprintf(file, "\tduration-stderr");
	if (perfstats) fprintf(file, "\tduration-t");
	if (perfstats) fprintf(file, "\tduration-min");
	if (perfstats) fprintf(file, "\tduration-max");
	if (perfstats) fprintf(file, "\tduration-range");
	if (perfstats) fprintf(file, "\tduration-n");
	if (withbench) {
		fprintf(file, "\tbench-count-ubmean");
		if (!setting_fewer_stats) fprintf(file, "\tbench-count-median");
		if (!perfonly) fprintf(file, "\tbench-count-mean");
		if (!setting_fewer_stats) fprintf(file, "\tbench-count-stdev");
		if (!perfonly) fprintf(file, "\tbench-count-stderr");
		if (!perfonly) fprintf(file, "\tbench-count-min");
		if (!perfonly) fprintf(file, "\tbench-count-max");
		if (!perfonly) fprintf(file, "\tbench-count-n");
		fprintf(file, "\tbench-rate-ubmean");
		if (!setting_fewer_stats) fprintf(file, "\tbench-rate-median");
		if (!perfonly) fprintf(file, "\tbench-rate-mean");
		if (!setting_fewer_stats) fprintf(file, "\tbench-rate-stdev");
		if (!perfonly) fprintf(file, "\tbench-rate-stderr");
		if (!perfonly) fprintf(file, "\tbench-rate-min");
		if (!perfonly) fprintf(file, "\tbench-rate-max");
		if (!perfonly) fprintf(file, "\tbench-rate-n");
		fprintf(file, "\tbench-time-median");
		if (!perfonly) fprintf(file, "\tbench-time-mean");
		if (!setting_fewer_stats) fprintf(file, "\tbench-time-stdev");
		if (!perfonly) fprintf(file, "\tbench-time-stderr");
		if (!perfonly) fprintf(file, "\tbench-time-min");
		if (!perfonly) fprintf(file, "\tbench-time-max");
		if (!perfonly) fprintf(file, "\tbench-time-n");
	}
	if (!perfonly) fprintf(file, "\tcount-pass");
	if (!perfonly) fprintf(file, "\tcount-fail");
	if (!perfonly) fprintf(file, "\tcount-crash");
	fprintf(file, "\n");
	dump_stats_per_test_for_module(file, NULL, NULL, perfonly, withbench);
	if (!perfonly) {
		dump_stats_per_test_per_module(file, withbench);
		dump_stats_per_test_per_path(file, pathlist, withbench);
	}
}

static FILE *openoutfile(const char *path, const char *mode) {
	FILE *file;

	assert(path);

	file = fopen(path, mode);
	if (!file) {
		fprintf(stderr, "error: cannot create %s: %s\n",
			path, strerror(errno));
		exit(-1);
	}
	return file;
}

static void dump_stats_per_test(
	const char *pathout,
	const struct pathlist *pathlist,
	int perfonly) {
	FILE *file;

	assert(pathout);
	assert(pathlist || perfonly);

	file = openoutfile(pathout, "w");
	dump_stats_per_test_with_file(file, pathlist, perfonly);
	fclose(file);
}

struct callsite_record {
	uint64_t id;
	const char *module;
	int closed_count;
};

static size_t callsites_enumerate(struct callsite_record *callsites) {
	struct callsite_record *callsite;
	int closed_count;
	size_t count = 0, i;
	const struct minixtest_stats_module *module;

	for (module = stats.total.modules; module; module = module->next) {
		if (!module->callsites_closed) continue;
		for (i = 0; i < CALLSITE_ID_MAX; i++) {
			closed_count = module->callsites_closed[i];
			if (closed_count <= 0) continue;
			if (callsites) {
				callsite = &callsites[count];
				callsite->id = i;
				callsite->module = module->name;
				callsite->closed_count = closed_count;
			}
			count++;
		}
	}
	return count;
}

static int callsites_compare(const void *p1, const void *p2) {
	const struct callsite_record *cs1 = p1, *cs2 = p2;
	int r;

	if (cs1->closed_count > cs2->closed_count) return -1;
	if (cs1->closed_count < cs2->closed_count) return 1;
	r = strcmp(cs1->module, cs2->module);
	if (r) return r;
	if (cs1->id < cs2->id) return -1;
	if (cs1->id > cs2->id) return 1;
	return 0;
}

static void dump_stats_callsites_with_file(FILE *file) {
	struct callsite_record *callsite, *callsites;
	size_t count, i;

	/* get a list of callsites */
	count = callsites_enumerate(NULL);
	callsites = CALLOC(count, struct callsite_record);
	callsites_enumerate(callsites);

	/* sort by importance */
	qsort(callsites, count, sizeof(*callsites), callsites_compare);

	/* dump to file */
	fprintf(file,
		"module\t"
		"id\t"
		"closed_count\n");
	for (i = 0; i < count; i++) {
		callsite = &callsites[i];
		fprintf(file, "%s\t%llu\t%d\n",
			callsite->module,
			(long long) callsite->id,
			callsite->closed_count);
	}

	FREE(callsites);
}

static void dump_stats_callsites(const char *pathout) {
	FILE *file;

	file = openoutfile(pathout, "w");
	dump_stats_callsites_with_file(file);
	fclose(file);
}

static int sum_classification(
	enum run_outcome outcome,
	int pretest,
	int recovery) {
	enum run_outcome o, o1, o2;
	int p, p1, p2;
	int r, r1, r2;
	int total = 0;

	if (outcome < run_outcome_count) {
		o1 = o2 = outcome;
	} else {
		o1 = 0;
		o2 = run_outcome_count - 1;
	}
	if (pretest < 2) {
		p1 = p2 = pretest;
	} else {
		p1 = 0;
		p2 = 1;
	}
	if (recovery < 2) {
		r1 = r2 = recovery;
	} else {
		r1 = 0;
		r2 = 1;
	}
	for (o = o1; o <= o2; o++) {
	for (p = p1; p <= p2; p++) {
	for (r = r1; r <= r2; r++) {
		total += stats.classification[o][p][r];
	}
	}
	}
	return total;
}

static void dump_classification(FILE *file) {
	int i;
	enum run_outcome outcome;
	int pretest, recovery, sum;

	/* pretest header row */
	fprintf(file, "pretest");
	for (i = 0; i < 9; i++) {
		pretest = i / 3;
		switch (pretest) {
		case 0: fprintf(file, "\tfail"); break;
		case 1: fprintf(file, "\tpass"); break;
		default: fprintf(file, "\ttotal"); break;
		}
	}
	fprintf(file, "\n");

	/* recovery header row */
	fprintf(file, "recover");
	for (i = 0; i < 9; i++) {
		recovery = i % 3;
		switch (recovery) {
		case 0: fprintf(file, "\tno"); break;
		case 1: fprintf(file, "\tyes"); break;
		default: fprintf(file, "\ttotal"); break;
		}
	}
	fprintf(file, "\n");

	/* data rows */
	for (outcome = 0; outcome <= run_outcome_count; outcome++) {
		fprintf(file, "%s", outcome_to_str(outcome));
		for (i = 0; i < 9; i++) {
			pretest = i / 3;
			recovery = i % 3;
			sum = sum_classification(outcome, pretest, recovery);
			fprintf(file, "\t%d", sum);
		}
		fprintf(file, "\n");
	}
}

static void dump_test_stats_line(
	FILE *file,
	struct stats *stats,
	const char *metric,
	const char *reference) {
	int digits = 1;

	fprintf(file, "%s\t%s\t%.*f\t%.1f\t%.1f\t%d\t%.*f\t%.*f\n",
		metric,
		reference ? : "",
		digits,
		stats_mean(stats),
		stats_median(stats),
		stats_stdev(stats),
		stats->n,
		digits,
		stats_min(stats),
		digits,
		stats_max(stats));
}

static void dump_test_stats(FILE *file) {
	fprintf(file, "metric\tref\tmean\tmedian\tstdev\tn\tmin\tmax\n");
	dump_test_stats_line(file, &stats.test_cnt_planned,      "planned",  NULL);
	dump_test_stats_line(file, &stats.test_cnt_started,      "started",  NULL);
	dump_test_stats_line(file, &stats.test_cnt_completed,    "compl",    NULL);
	dump_test_stats_line(file, &stats.test_cnt_passed,       "passed",   NULL);
	dump_test_stats_line(file, &stats.test_cnt_failed,       "failed",   NULL);
	dump_test_stats_line(file, &stats.test_cnt_crashed,      "crashed",  NULL);
	dump_test_stats_line(file, &stats.test_cnt_unreached,    "unreach",  NULL);
	dump_test_stats_line(file, &stats.test_pct_pl_started,   "started",  "planned");
	dump_test_stats_line(file, &stats.test_pct_pl_completed, "compl",    "planned");
	dump_test_stats_line(file, &stats.test_pct_pl_passed,    "passed",   "planned");
	dump_test_stats_line(file, &stats.test_pct_pl_failed,    "failed",   "planned");
	dump_test_stats_line(file, &stats.test_pct_pl_crashed,   "crashed",  "planned");
	dump_test_stats_line(file, &stats.test_pct_pl_unreached, "unreach",  "planned");
	dump_test_stats_line(file, &stats.test_pct_st_completed, "compl",    "started");
	dump_test_stats_line(file, &stats.test_pct_st_passed,    "passed",   "started");
	dump_test_stats_line(file, &stats.test_pct_st_failed,    "failed",   "started");
	dump_test_stats_line(file, &stats.test_pct_st_crashed,   "crashed",  "started");
	dump_test_stats_line(file, &stats.test_pct_cp_passed,    "passed",   "compl");
	dump_test_stats_line(file, &stats.test_pct_cp_failed,    "failed",   "compl");
}

static void lockfile(FILE *file, short type) {
	int fd;
	struct flock flock = { .l_type = type };

	fd = fileno(file);
	if (fd < 0) {
		perror("error: cannot get fd for file");
		exit(-1);
	}
	if (fcntl(fd, F_SETLKW, &flock) == -1) {
		perror("error: cannot get lock for file");
		exit(-1);
	}
}

static void dump_latex_line(FILE *file, const char *label, int pretest) {
	int count_pass, count_fail, count_shutdown, count_crash, count_total;

	if (setting_pretest_count <= 0 && pretest < 2) return;

	count_pass = sum_classification(ro_success, pretest, 2);
	count_fail = sum_classification(ro_failed, pretest, 2);
	count_shutdown = sum_classification(ro_shutdown, pretest, 2);
	count_crash = sum_classification(ro_crash, pretest, 2) +
		sum_classification(ro_timeout, pretest, 2);
	count_total = count_pass + count_fail + count_shutdown + count_crash;
	if (count_total > 0) {
		fprintf(file, "%-20s",
			label);
		if (setting_pretest_count > 0) {
			fprintf(file, " & %-15s",
				(pretest == 0) ? "fail" : (pretest == 1) ? "pass" : "both");
		}
		fprintf(file, " & %10.1f\\%% & %10.1f\\%% & %14.1f\\%% & %11.1f\\%% & %21.1f\\%% \\\\\n",
			count_pass * 100.0 / count_total,
			count_fail * 100.0 / count_total,
			count_shutdown * 100.0 / count_total,
			count_crash * 100.0 / count_total,
			stats_mean(&stats.test_pct_pl_completed));
	}
}

static void dump_latex_with_file(FILE *file, const char *label) {
	off_t off;
	int pretest;

	if (!label) label = "(recovery mode)";

	off = ftello(file);
	if (off == -1) {
		perror("error: cannot get offset for file");
		exit(-1);
	}
	if (off == 0) {
		fprintf(file, "\\begin{tabular}[b]{l   l               |              r              r                  r             r |                       r |}\n");
		fprintf(file, "\\tabh{Recovery mode}");
		if (setting_pretest_count > 0) {
			fprintf(file, " & \\tabsh{Pretest}");
		}
		fprintf(file, " & \\tabsh{Pass} & \\tabsh{Fail} & \\tabsh{Shutdown} & \\tabsh{Crash} & \\tabsh{Tests completed} \\\\ [0.5ex]\n");
		fprintf(file, "\\toprule\n");
	}

	for (pretest = 1; pretest <= 2; pretest++) {
		dump_latex_line(file, label, pretest);
	}
	
	if (off == 0) {
		/* manually move after last line after table is complete */
		fprintf(file, "\\bottomrule\n");
		fprintf(file, "\\end{tabular}\n");
	}
}

static void dump_latex(const char *pathout, const char *label) {
	FILE *file;

	file = openoutfile(pathout, "a");
	lockfile(file, F_WRLCK);
	dump_latex_with_file(file, label);
	lockfile(file, F_UNLCK);
	fclose(file);
}

static void usage(const char *msg) {
	if (*msg) {
		printf("%s\n", msg);
		printf("\n");
	}
	printf("usage:\n");
	printf("  minixtestloganalyze [-npsS] [-c outpath] [-l outfile [-L label]]\n");
	printf("                      [-m maxn] [-P pretestcount] [-r outpath] [-t outpath]\n");
	printf("                      [-T outfile] [-x module]... [-x module]... [-X test]...\n");
	printf("                      mappath... logpath...\n");
	printf("\n");
	printf("options:\n");
	printf("-c overview of callsite ids closing the window.\n");
	printf("-h show usage information\n");
	printf("-l append line for LaTeX table in paper.\n");
	printf("-L label for line in LaTeX table in paper.\n");
	printf("-n naive mode: detected deadlocks considered hangs.\n");
	printf("-m max count: stop after succesfully reading maxn logs.\n");
	printf("-p show progress.\n");
	printf("-P mark the first pretestcount tests as pretests.\n");
	printf("-r write per-run overview.\n");
	printf("-s include extra statistics.\n");
	printf("-S include fewer statistics.\n");
	printf("-t write per-test, per-module, per-source file overview.\n");
	printf("-T write per-test overview of performance.\n");
	printf("-x exclude the specified module from the results.\n");
	printf("-X exclude the specified test from the results.\n");
	exit(*msg? 1 : 0);
}

int main(int argc, char **argv) {
	FILE *fileperrun = NULL;
	struct function_hashtable *functions;
	const char *labellatex = NULL;
	const char *path;
	const char *pathcallsites = NULL;
	const char *pathlatex = NULL;
	const char *pathperrun = NULL;
	const char *pathpertest = NULL;
	const char *pathpertestperf = NULL;
	struct pathlist pathlist;
	int r;

	/* handle options */
	while ((r = getopt(argc, argv, "c:hl:L:m:npP:r:sSt:T:x:X:")) >= 0) {
		switch (r) {
		case 'c':
			if (pathcallsites) usage("-c specified multiple times");
			pathcallsites = optarg;
			break;
		case 'h':
			usage("");
			break;
		case 'l':
			if (pathlatex) usage("-l specified multiple times");
			pathlatex = optarg;
			break;
		case 'L':
			if (labellatex) usage("-L specified multiple times");
			labellatex = optarg;
			break;
		case 'm':
			if (setting_max_count != INT_MAX) usage("-m specified multiple times");
			setting_max_count = atoi(optarg);
			if (setting_max_count <= 0) usage("invalid maxn specified");
			break;
		case 'n':
			setting_naive_mode = 1;
			break;
		case 'p':
			setting_show_progress = 1;
			break;
		case 'P':
			if (setting_pretest_count) usage("-P specified multiple times");
			setting_pretest_count = atoi(optarg);
			if (setting_pretest_count <= 0) usage("invalid pretestcount specified");
			break;
		case 'r':
			if (pathperrun) usage("-r specified multiple times");
			pathperrun = optarg;
			break;
		case 's':
			setting_extra_stats = 1;
			break;
		case 'S':
			setting_fewer_stats = 1;
			break;
		case 't':
			if (pathpertest) usage("-t specified multiple times");
			pathpertest = optarg;
			break;
		case 'T':
			if (pathpertestperf) usage("-T specified multiple times");
			pathpertestperf = optarg;
			break;
		case 'x':
			string_ll_add(&excludemod, optarg);
			break;
		case 'X':
			string_ll_add(&excludetest, optarg);
			break;
		default:
			usage("Unknown flag specified");
			break;
		}
	}

	if (labellatex && !pathlatex) usage("-L option requires -l");

	if (pathperrun) fileperrun = openoutfile(pathperrun, "w");

	/* load map files */
	while (optind < argc) {
		path = argv[optind++];
		if (ends_with(path, ".map", 1)) {
			load_map(path);
		} else {
			load_log(path, fileperrun);
		}
	}

	if (fileperrun) fclose(fileperrun);

	/* make overview of functions */
	functions = functions_build_table(modules);
	minixtest_stats_init_coverage(&stats, functions);

	/* dump statistics */
	if (pathpertest) {
		pathlist_init(&pathlist, functions);
		dump_stats_per_test(pathpertest, &pathlist, 0 /*perfonly*/);
		pathlist_free(&pathlist);
	}
	if (pathpertestperf) {
		dump_stats_per_test(pathpertestperf, NULL, 1 /*perfonly*/);
	}
	if (pathcallsites) {
		dump_stats_callsites(pathcallsites);
	}
	if (pathlatex) {
		dump_latex(pathlatex, labellatex);
	}
	dump_classification(stdout);
	printf("\n");
	dump_test_stats(stdout);

	minixtest_stats_free(&stats);
	functions_free(functions);
	modules_free(modules);
	modules = NULL;
	return 0;
}
