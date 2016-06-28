#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "function.h"
#include "helper.h"
#include "minixteststats.h"
#include "module.h"

struct faultstats_one {
	execcount faultact;
	int faultinj;
	struct minixtest_log_summary_recoveries recoveries;
};

double stats_max(struct stats *stats) {
	return stats_percentile(stats, 100);
}

double stats_min(struct stats *stats) {
	return stats_percentile(stats, 0);
}

double stats_median(struct stats *stats) {
	return stats_percentile(stats, 50);
}

static int stats_sort_compare(const void *p1, const void *p2) {
	const double *v1 = p1;
	const double *v2 = p2;

	assert(p1);
	assert(p2);

	if (*v1 < *v2) return -1;
	if (*v1 > *v2) return 1;
	return 0;
}

static void stats_sort(struct stats *stats) {
	assert(stats);

	if (stats->valuessorted) return;

	qsort(stats->values, stats->n, sizeof(*stats->values),
		stats_sort_compare);
	stats->valuessorted = 1;
}

double stats_mean_trunc(struct stats *stats, int remove_low, int remove_high) {
	int i, n;
	double sum = 0;

	assert(stats);
	assert(remove_low >= 0);
	assert(remove_high >= 0);

	n = stats->n - remove_low - remove_high;
	assert(n >= 0);
	if (n <= 0) return NAN;

	stats_sort(stats);

	for (i = 0; i < n; i++) {
		sum += stats->values[i + remove_low];
	}
	return sum / n;
}

double stats_mean_unixbench(struct stats *stats) {
	assert(stats);

	return stats_mean_trunc(stats, stats->n / 3, 0);
}

double stats_percentile(struct stats *stats, double percentile) {
	double factor;
	double index;
	int index_int;
	double result;

	assert(stats);
	assert(percentile >= 0);
	assert(percentile <= 100);

	if (stats->n <= 0) return NAN;

	stats_sort(stats);

	index = percentile * (stats->n - 1) / 100;
	index_int = (int) floor(index);
	factor = index - index_int;
	result = (1 - factor) * stats->values[index_int];
	if (index_int + 1 < stats->n) {
		result += factor * stats->values[index_int + 1];
	}
	return result;
}

double stats_mean(const struct stats *stats) {
	assert(stats);

	return (stats->n > 0) ? (stats->sum / stats->n) : NAN;
}

double stats_stderr(const struct stats *stats) {
	assert(stats);

	return (stats->n > 0) ? sqrt(stats_var(stats) / stats->n) : NAN;
}

double stats_stdev(const struct stats *stats) {
	assert(stats);

	return sqrt(stats_var(stats));
}

double stats_var(const struct stats *stats) {
	assert(stats);

	if (stats->n > 1) {
		return (stats->sum2 - stats->sum * stats->sum / stats->n) /
			(stats->n - 1);
	} else {
		return NAN;
	}
}

static void stats_add(struct stats *stats, double value) {
	assert(stats);
	assert(stats->n >= 0);

	if (isnan(value)) return;

	if (stats->n >= stats->valuessize) {
		if (stats->valuessize < 32) {
			stats->valuessize = 32;
		} else {
			stats->valuessize *= 2;
		}
		stats->values = REALLOC(stats->values, stats->valuessize, double);
	}

	stats->values[stats->n++] = value;
	stats->valuessorted = 0;
	stats->sum += value;
	stats->sum2 += value * value;
}

void stats_free(struct stats *stats) {
	assert(stats);

	if (stats->values) {
		FREE(stats->values);
		stats->values = NULL;
	}
}

static void stats_add_pct(struct stats *stats, int count, int total) {
	if (total > 0) stats_add(stats, count * 100.0 / total);
}

static int stats_compute_duration(
	struct timeval *diff,
	const struct minixtest_log_summary *summary,
	const char *name_start,
	const struct timeval *timestamp_start,
	const char *name_end,
	const struct timeval *timestamp_end) {

	assert(diff);
	assert(summary);
	assert(name_start);
	assert(timestamp_start);
	assert(name_end);
	assert(timestamp_end);

	if (timeval_is_zero(timestamp_start)) {
		fprintf(stderr, "warning: timestamp for %s not set in "
			"log file %s\n", name_start, summary->logpath);
		memset(diff, 0, sizeof(*diff));
		return 0;
	}
	if (timeval_is_zero(timestamp_end))  {
		fprintf(stderr, "warning: timestamp for %s not set in "
			"log file %s\n", name_end, summary->logpath);
		memset(diff, 0, sizeof(*diff));
		return 0;
	}
	if (timeval_compare(timestamp_start, timestamp_end) > 0) {
		fprintf(stderr, "warning: timestamps for %s and %s incorrectly "
			"ordered in log file %s\n",
			name_start, name_end, summary->logpath);
		memset(diff, 0, sizeof(*diff));
		return 0;
	}

	*diff = *timestamp_end;
	timeval_subtract(diff, timestamp_start);
	return 1;
}

static void stats_add_duration(
	struct stats *duration,
	const struct minixtest_log_summary *summary,
	const char *name_start,
	const struct timeval *timestamp_start,
	const char *name_end,
	const struct timeval *timestamp_end) {
	struct timeval diff;

	assert(duration);
	assert(summary);
	assert(name_start);
	assert(timestamp_start);
	assert(name_end);
	assert(timestamp_end);

	if (!stats_compute_duration(&diff, summary, name_start, timestamp_start,
		name_end, timestamp_end)) {
		return;
	}
	stats_add(duration, timeval_seconds(&diff));
}

static struct minixtest_stats_module *minixtest_stats_get_module(
	struct minixtest_stats_module **modules_p,
	const char *name) {
	struct minixtest_stats_module *module, **module_p;
	const char *nameend;
	size_t namelen;

	assert(modules_p);
	assert(name);

	nameend = strchr(name, '@');
	namelen = nameend ? (nameend - name) : strlen(name);
	for (module_p = modules_p; (module = *module_p); module_p = &module->next) {
		if (strlen(module->name) == namelen &&
			strncmp(module->name, name, namelen) == 0) {
			return module;
		}
	}

	module = *module_p = CALLOC(1, struct minixtest_stats_module);
	module->name = STRNDUP(name, namelen);
	return module;
}

static struct minixtest_stats_test *minixtest_stats_get_test(
	struct minixtest_stats *stats,
	const char *name) {
	struct minixtest_stats_test *test, **test_p;

	assert(stats);
	assert(name);

	for (test_p = &stats->tests; (test = *test_p); test_p = &test->next) {
		if (strcmp(test->name, name) == 0) return test;
	}

	test = *test_p = CALLOC(1, struct minixtest_stats_test);
	test->name = STRDUP(name);
	return test;
}

static void minixtest_stats_add_module(
	struct minixtest_stats_module **modules_p,
	const char *name,
	size_t bb_count,
	execcount *bbs) {
	size_t i;
	struct minixtest_stats_module *module;

	assert(modules_p);
	assert(name);
	if (!bbs) return;
	assert(bb_count > 0);

	module = minixtest_stats_get_module(modules_p, name);
	if (module->bb_count == 0) {
		module->bb_count = bb_count;
	} else if (module->bb_count != bb_count) {
		fprintf(stderr, "warning: inconsistent bb counts %zu and %zu "
			"when adding module %s\n",
			module->bb_count, bb_count, name);
		return;
	}

	if (!module->bbs) module->bbs = CALLOC(bb_count, execcount);

	module->count++;
	for (i = 0; i < bb_count; i++) {
		module->bbs[i] += bbs[i];
	}
}

static execcount *bbs_sub(const execcount *bbs1, const execcount *bbs2,
	size_t bb_count) {
	execcount *bbs = MALLOC(bb_count, execcount);
	size_t i;

	for (i = 0; i < bb_count; i++) {
		bbs[i] = bbs1 ? (bbs1[i] - (bbs2 ? bbs2[i] : 0)) : 0;
	}
	return bbs;
}

static const struct minixtest_log_summary_test_module *find_module(
	const struct minixtest_log_summary_test *sumtest,
	const char *name) {
	const struct minixtest_log_summary_test_module *summod;

	assert(sumtest);
	assert(name);

	for (summod = sumtest->modules; summod; summod = summod->next) {
		if (strcmp(name, summod->name) == 0) return summod;
	}
	return NULL;
}

void recoveries_add(
	struct minixtest_log_summary_recoveries *dst,
	const struct minixtest_log_summary_recoveries *src,
	int factor) {
	enum ltckpt_method method;
	enum ltckpt_status status;

	assert(dst);
	assert(src);

	for (status = 0; status < ltckpt_status_count; status++) {
	for (method = 0; method < ltckpt_method_count; method++) {
		dst->count[status][method] += src->count[status][method] * factor;
	}
	}
}

static void faultstats_add(
	struct faultstats *dst,
	const struct faultstats_one *src) {
	enum ltckpt_method method;
	enum ltckpt_status status;

	assert(dst);
	assert(src);
		
	stats_add(&dst->faultact, src->faultact);
	stats_add(&dst->faultactany, src->faultact > 0);
	stats_add(&dst->faultinj, src->faultinj > 0);

	for (status = 0; status < ltckpt_status_count; status++) {
	for (method = 0; method < ltckpt_method_count; method++) {
		stats_add(&dst->recoveries[status][method],
			src->recoveries.count[status][method]);
		stats_add(&dst->recoveryany[status][method],
			src->recoveries.count[status][method] > 0);
	}
	}
}

static void faultstats_free(struct faultstats *faultstats) {
	enum ltckpt_method method;
	enum ltckpt_status status;

	assert(faultstats);
	
	stats_free(&faultstats->faultact);
	stats_free(&faultstats->faultactany);
	stats_free(&faultstats->faultinj);
	for (status = 0; status < ltckpt_status_count; status++) {
	for (method = 0; method < ltckpt_method_count; method++) {
		stats_free(&faultstats->recoveries[status][method]);
		stats_free(&faultstats->recoveryany[status][method]);
	}
	}
}

static void minixtest_stats_add_test_module_diff(
	struct minixtest_stats *stats,
	const struct minixtest_log_summary_test_module *summod,
	const struct minixtest_log_summary_test *sumtest_next) {
	execcount *bbs_diff;
	const struct minixtest_log_summary_test_module *summod_next;

	assert(stats);
	assert(summod);
	assert(sumtest_next);

	summod_next = find_module(sumtest_next, summod->name);
	if (!summod_next) return;

	if (summod_next->bb_count != summod->bb_count) {
		fprintf(stderr, "warning: inconsistent bb counts "
			"%zu and %zu when computing diff for module %s\n",
			summod_next->bb_count, summod->bb_count,
			summod->name);
		return;
	}

	bbs_diff = bbs_sub(summod_next->bbs_before,
		summod->bbs_after, summod->bb_count);
	minixtest_stats_add_module(&stats->between.modules,
		summod->name, summod->bb_count, bbs_diff);
	minixtest_stats_add_module(&stats->total.modules,
		summod->name, summod->bb_count, bbs_diff);
	FREE(bbs_diff);
}

static void minixtest_stats_add_test_module(
	struct minixtest_stats *stats,
	struct minixtest_stats_test *statest,
	struct faultstats_one *faultstats,
	const struct minixtest_log_summary_test *sumtest,
	const struct minixtest_log_summary_test_module *summod,
	int is_first) {

	assert(stats);
	assert(statest);
	assert(faultstats);
	assert(summod);

	minixtest_stats_add_module(&statest->modules, summod->name,
		summod->bb_count, summod->bbs_during);
	minixtest_stats_add_module(&stats->alltests.modules, summod->name,
		summod->bb_count, summod->bbs_during);
	minixtest_stats_add_module(&stats->total.modules, summod->name,
		summod->bb_count, summod->bbs_during);
	if (sumtest->next) {
		minixtest_stats_add_test_module_diff(
			stats, summod, sumtest->next);
	}
	if (is_first) {
		minixtest_stats_add_module(&stats->boot.modules, summod->name,
			summod->bb_count, summod->bbs_before);
		minixtest_stats_add_module(&stats->total.modules, summod->name,
			summod->bb_count, summod->bbs_before);
	}

	faultstats->faultact += summod->faultact;
	recoveries_add(&faultstats->recoveries, &summod->recoveries, 1);
}

static void coverage_add_module(
	struct coverage_per_bb *coverage,
	const struct minixtest_stats_module *stamod) {
	const struct module *mapmod;

	assert(coverage);
	assert(coverage->functions);
	assert(stamod);
	assert(stamod->name);

	mapmod = module_find(coverage->functions->modules, stamod->name);
	if (!mapmod) {
		fprintf(stderr, "warning: module %s in logs but not "
			"in map file\n", stamod->name);
		return;
	}
	if (stamod->bb_count <= 0) {
		fprintf(stderr, "warning: module %s has no basic blocks\n",
			stamod->name);
		return;
	}
	coverage_add_execcount_module(
		coverage,
		mapmod,
		stamod->bb_count,
		stamod->bbs);
}

static void coverage_add_modules(
	struct coverage_per_bb *coverage,
	const struct minixtest_stats_module *modules) {
	const struct minixtest_stats_module *module;

	assert(coverage);

	for (module = modules; module; module = module->next) {
		CHECKPTR(module);
		coverage_add_module(coverage, module);
	}
}
	
static void minixtest_stats_add_test(
	struct minixtest_stats *stats,
	struct faultstats_one *faultstatsalltests,
	const struct minixtest_log_summary *summary,
	const struct minixtest_log_summary_test *sumtest,
	int is_first) {
	const struct minixtest_log_summary_test_module *summod;
	struct minixtest_stats_test *statest;
	struct faultstats_one faultstats = { };

	assert(stats);
	assert(sumtest);
	assert(faultstatsalltests);

	statest = minixtest_stats_get_test(stats, sumtest->name);
	switch (sumtest->status) {
	case mts_passed: statest->count_pass++; break;
	case mts_failed: statest->count_fail++; break;
	case mts_running: statest->count_crash++; break;
	}
	stats_add_duration(&statest->duration, summary,
		sumtest->name, &sumtest->timestamp_start,
		"end of test", &sumtest->timestamp_end);
	stats_add(&statest->reported_count, sumtest->reported_count);
	stats_add(&statest->reported_rate, sumtest->reported_rate);
	stats_add(&statest->reported_time, sumtest->reported_time);

	for (summod = sumtest->modules; summod; summod = summod->next) {
		CHECKPTR(summod);
		minixtest_stats_add_test_module(
			stats,
			statest,
			&faultstats,
			sumtest,
			summod,
			is_first);
	}

	faultstats.faultinj = faultstatsalltests->faultinj;
	faultstats_add(&statest->faultstats, &faultstats);

	faultstatsalltests->faultact += faultstats.faultact;
	recoveries_add(&faultstatsalltests->recoveries,
		&faultstats.recoveries, 1);
}

static int iswhitespace(char c) {
	return c == ' ' || c == '\t' || c == '\n';
}

static void minixtest_stats_add_tests_unreached(
	struct minixtest_stats *stats,
	const char *testsunreached) {
	struct minixtest_stats_test *statest;
	const char *test;
	char *testtemp;

	if (!testsunreached) return;

	for (;;) {
		while (iswhitespace(*testsunreached)) testsunreached++;
		if (!*testsunreached) break;

		test = testsunreached;
		while (*testsunreached && !iswhitespace(*testsunreached)) {
			testsunreached++;
		}
		testtemp = STRNDUP(test, testsunreached - test);
		statest = minixtest_stats_get_test(stats, testtemp);
		statest->count_notreached++;
		FREE(testtemp);
	}
}

static void faultstats_sum(
	struct faultstats_one *faultstats,
	const struct minixtest_log_summary *summary) {
	const struct minixtest_log_summary_module *module;

	assert(faultstats);
	assert(summary);

	for (module = summary->modules; module; module = module->next) {
		CHECKPTR(module);
		if (module->faultindexretrieved > 0 && module->faultindex >= 0) {
			faultstats->faultinj++;
		}
		faultstats->faultact += module->faultact;
		recoveries_add(&faultstats->recoveries, &module->recoveries, 1);
	}
}

static const char *testsplanned_skip(
	const char *testsplanned,
	const char *test,
	const struct minixtest_log_summary *summary) {
	size_t len, lenplanned;
	const char *testplanned;

	assert(test);
	assert(summary);

	if (!testsplanned) return NULL;

	/* skip whitespace */
	while (iswhitespace(*testsplanned)) testsplanned++;

	/* skip name of currently planned test */
	testplanned = testsplanned;
	while (*testsplanned && !iswhitespace(*testsplanned)) {
		testsplanned++;
	}

	/* does skipped name match specified test? */
	len = strlen(test);
	lenplanned = testsplanned - testplanned;
	if (len == lenplanned && memcmp(testplanned, test, len) == 0) {
		return testsplanned;
	}

	/* mismatch */
	fprintf(stderr, "warning: expected test %.*s but found test %s in log file %s\n",
		(int) lenplanned, testplanned, test, summary->logpath);
	return testplanned;
}

static void run_classify(
	struct run_classification *classification,
	const struct minixtest_log_summary *summary,
	int pretest_count,
	int naive_mode) {
	int deadlock = 0;
	int failed = 0;
	enum ltckpt_method method;
	const struct minixtest_log_summary_module *module;
	int pretest_passed = 0;
	int pretest_recovery = 0;
	int shutdown = 0;
	const struct minixtest_log_summary_test *sumtest;
	int testindex = 0;
	const struct minixtest_log_summary_test_module *testmod;
	int total_recovery = 0;

	assert(classification);
	assert(summary);

	/* count total number of recoveries */
	for (module = summary->modules; module; module = module->next) {
		CHECKPTR(module);
		total_recovery += module->recoveries.count[ls_start][lm_generic];
		for (method = 0; method < ltckpt_method_count; method++) {
			if (module->recoveries.count[ls_shutdown][method] > 0) {
				shutdown = 1;
			}
		}
		if (naive_mode && module->recoveries.count[ls_shutdown][lm_deadlock] > 0) {
			deadlock = 1;
		}
	}

	/* count per-test info */
	for (sumtest = summary->tests; sumtest; sumtest = sumtest->next) {
		CHECKPTR(sumtest);
		if (sumtest->status == mts_passed) {
			if (testindex < pretest_count) pretest_passed++;
		} else {
			failed++;
		}
		if (testindex < pretest_count) {
			for (testmod = sumtest->modules; testmod; testmod = testmod->next) {
				pretest_recovery += testmod->recoveries.count[ls_start][lm_generic];
			}
		}
		testindex++;
	}

	memset(classification, 0, sizeof(*classification));
	if (deadlock) {
		classification->outcome = ro_timeout;
	} else if (shutdown) {
		classification->outcome = ro_shutdown;
	} else if (summary->status_reset) {
		classification->outcome = ro_crash;
	} else if (summary->signal > 0) {
		classification->outcome = ro_timeout;
	} else if (summary->status < mtls_tests_complete) {
		classification->outcome = ro_crash;
	} else {
		classification->outcome = (failed > 0) ? ro_failed : ro_success;
	}
	classification->pretest = pretest_passed >= pretest_count;
	classification->recovery = total_recovery > pretest_recovery;
}

static void minixtest_stats_add_callsites(
	struct minixtest_stats_test *test,
	const struct minixtest_log_summary *summary) {
	size_t i;
	struct minixtest_stats_module *stamod;
	const struct minixtest_log_summary_module *summod;

	assert(summary);

	for (summod = summary->modules; summod; summod = summod->next) {
		CHECKPTR(summod);
		stamod = minixtest_stats_get_module(&test->modules, summod->name);
		if (!stamod->callsites_closed) {
			stamod->callsites_closed = CALLOC(CALLSITE_ID_MAX, int);
		}
		for (i = 0; i < CALLSITE_ID_MAX; i++) {
			stamod->callsites_closed[i] += summod->callsites_closed[i];
		}
	}
}

void minixtest_stats_add(
	struct minixtest_stats *stats,
	struct run_classification *classification,
	const struct minixtest_log_summary *summary,
	int pretest_count,
	int naive_mode) {
	struct timeval duration_alltests;
	struct timeval duration_alltests_sum;
	struct timeval duration_between;
	struct timeval duration_between_sum;
	struct faultstats_one faultstatsalltests = { };
	struct faultstats_one faultstatsboot = { };
	struct faultstats_one faultstatstotal = { };
	const char *name_prev;
	const struct minixtest_log_summary_test *sumtest;
	const char *testsplanned;
	int test_cnt_planned;
	int test_cnt_started = 0;
	int test_cnt_completed;
	int test_cnt_passed = 0;
	int test_cnt_failed = 0;
	int test_cnt_crashed = 0;
	int test_cnt_unreached;
	const struct timeval *timestamp_prev;
	const struct timeval *timestamp_test_first;

	assert(stats);
	assert(classification);
	assert(summary);

	/* assign names to phases */
	if (!stats->boot.name) stats->boot.name = STRDUP("<boot>");
	if (!stats->between.name) stats->between.name = STRDUP("<between>");
	if (!stats->alltests.name) stats->alltests.name = STRDUP("<all-tests>");
	if (!stats->total.name) stats->total.name = STRDUP("<total>");

	/* update faultstats for the total */
	faultstats_sum(&faultstatstotal, summary);
	faultstats_add(&stats->total.faultstats, &faultstatstotal);

	/* compute duration of boot */
	timestamp_test_first = summary->tests ?
		&summary->tests->timestamp_start :
		&summary->timestamp_exit;
	stats_add_duration(&stats->boot.duration, summary,
		"QEMU start", &summary->timestamp_start,
		"first test start", timestamp_test_first);

	/* gather statistics for the individual tests */
	faultstatsalltests.faultinj = faultstatstotal.faultinj;
	memset(&duration_alltests_sum, 0, sizeof(duration_alltests_sum));
	memset(&duration_between_sum, 0, sizeof(duration_between_sum));
	timestamp_prev = timestamp_test_first;
	name_prev = "first test start";
	testsplanned = summary->testsplanned;
	for (sumtest = summary->tests; sumtest; sumtest = sumtest->next) {
		CHECKPTR(sumtest);

		stats_compute_duration(&duration_alltests, summary,
			sumtest->name, &sumtest->timestamp_start,
			sumtest->name, &sumtest->timestamp_end);
		timeval_add(&duration_alltests_sum, &duration_alltests);

		stats_compute_duration(&duration_between, summary,
			name_prev, timestamp_prev,
			sumtest->name, &sumtest->timestamp_start);
		timeval_add(&duration_between_sum, &duration_between);

		timestamp_prev = &sumtest->timestamp_end;
		name_prev = sumtest->name;

		minixtest_stats_add_test(stats, &faultstatsalltests,
			summary, sumtest, sumtest == summary->tests);

		testsplanned = testsplanned_skip(testsplanned, sumtest->name, summary);

		test_cnt_started++;
		switch (sumtest->status) {
			case mts_running: test_cnt_crashed++; break;
			case mts_passed: test_cnt_passed++; break;
			case mts_failed: test_cnt_failed++; break;
		}
	}
	faultstats_add(&stats->alltests.faultstats, &faultstatsalltests);

	/* add tests that were never reached */
	minixtest_stats_add_tests_unreached(stats, testsplanned);

	/* compute duration of shutdown */
	stats_add(&stats->between.duration,
		timeval_seconds(&duration_between_sum));
	stats_add_duration(&stats->total.duration, summary,
		"QEMU start", &summary->timestamp_start,
		"QEMU exit", &summary->timestamp_exit);

	/* compute faultstats during boot time */
	faultstatsboot.faultact = faultstatstotal.faultact -
		faultstatsalltests.faultact;
	faultstatsboot.faultinj = faultstatstotal.faultinj;
	faultstatsboot.recoveries = faultstatstotal.recoveries;
	recoveries_add(&faultstatsboot.recoveries,
		&faultstatsalltests.recoveries, -1);
	faultstats_add(&stats->boot.faultstats, &faultstatsboot);

	/* run classification */
	run_classify(classification, summary, pretest_count, naive_mode);
	stats->classification[classification->outcome][classification->pretest][classification->recovery]++;

	/* number of tests */
	test_cnt_planned = wordcount(summary->testsplanned);
	test_cnt_completed = test_cnt_passed + test_cnt_failed;
	test_cnt_unreached = test_cnt_passed + test_cnt_failed;
	if (test_cnt_unreached < 0) test_cnt_unreached = 0;
	stats_add(&stats->test_cnt_planned,   test_cnt_planned);
	stats_add(&stats->test_cnt_started,   test_cnt_started);
	stats_add(&stats->test_cnt_completed, test_cnt_completed);
	stats_add(&stats->test_cnt_passed,    test_cnt_passed);
	stats_add(&stats->test_cnt_failed,    test_cnt_failed);
	stats_add(&stats->test_cnt_crashed,   test_cnt_crashed);
	stats_add(&stats->test_cnt_unreached, test_cnt_unreached);
	stats_add_pct(&stats->test_pct_pl_started,   test_cnt_started,   test_cnt_planned);
	stats_add_pct(&stats->test_pct_pl_completed, test_cnt_completed, test_cnt_planned);
	stats_add_pct(&stats->test_pct_pl_passed,    test_cnt_passed,    test_cnt_planned);
	stats_add_pct(&stats->test_pct_pl_failed,    test_cnt_failed,    test_cnt_planned);
	stats_add_pct(&stats->test_pct_pl_crashed,   test_cnt_crashed,   test_cnt_planned);
	stats_add_pct(&stats->test_pct_pl_unreached, test_cnt_unreached, test_cnt_planned);
	stats_add_pct(&stats->test_pct_st_completed, test_cnt_completed, test_cnt_started);
	stats_add_pct(&stats->test_pct_st_passed,    test_cnt_passed,    test_cnt_started);
	stats_add_pct(&stats->test_pct_st_failed,    test_cnt_failed,    test_cnt_started);
	stats_add_pct(&stats->test_pct_st_crashed,   test_cnt_crashed,   test_cnt_started);
	stats_add_pct(&stats->test_pct_cp_passed,    test_cnt_passed,    test_cnt_completed);
	stats_add_pct(&stats->test_pct_cp_failed,    test_cnt_failed,    test_cnt_completed);

	/* call sites (total only) */
	minixtest_stats_add_callsites(&stats->total, summary);
}

const struct minixtest_stats_test *minixtest_stats_find_test(
	const struct minixtest_stats *stats,
	const char *testname) {
	const struct minixtest_stats_test *test;

	assert(stats);
	assert(testname);

	for (test = stats->tests; test; test = test->next) {
		if (strcmp(test->name, testname) == 0) return test;
	}
	return NULL;
}

static void minixtest_stats_init_coverage_test(
	struct minixtest_stats_test *test,
	const struct function_hashtable *functions) {

	assert(test);
	assert(functions);

	coverage_init(&test->coverage, functions);
	coverage_add_modules(&test->coverage, test->modules);
}

static void minixtest_stats_init_coverage_tests(
	struct minixtest_stats_test *tests,
	const struct function_hashtable *functions) {
	struct minixtest_stats_test *test;

	assert(functions);

	for (test = tests; test; test = test->next) {
		minixtest_stats_init_coverage_test(test, functions);
	}
}

void minixtest_stats_init_coverage(
	struct minixtest_stats *stats,
	const struct function_hashtable *functions) {

	assert(stats);
	assert(functions);

	minixtest_stats_init_coverage_tests(stats->tests, functions);
	minixtest_stats_init_coverage_test(&stats->boot, functions);
	minixtest_stats_init_coverage_test(&stats->between, functions);
	minixtest_stats_init_coverage_test(&stats->alltests, functions);
	minixtest_stats_init_coverage_test(&stats->total, functions);
}

const struct minixtest_stats_module *minixtest_stats_find_module(
	const struct minixtest_stats_test *test,
	const char *name) {
	const struct minixtest_stats_module *module;

	assert(test);
	assert(name);

	for (module = test->modules; module; module = module->next) {
		if (strcmp(module->name, name) == 0) return module;
	}
	return NULL;
}

static void minixtest_stats_free_module(struct minixtest_stats_module *module) {
	assert(module);

	if (module->name) FREE(module->name);
	if (module->bbs) FREE(module->bbs);
	if (module->callsites_closed) FREE(module->callsites_closed);
	memset(module, 0, sizeof(*module));
}

static void minixtest_stats_free_modules(
	struct minixtest_stats_module *modules) {
	struct minixtest_stats_module *next, *module;

	module = modules;
	while (module) {
		next = module->next;
		minixtest_stats_free_module(module);
		FREE(module);
		module = next;
	}
}

static void minixtest_stats_free_test(struct minixtest_stats_test *test) {
	assert(test);

	if (test->name) FREE(test->name);
	stats_free(&test->duration);
	stats_free(&test->reported_count);
	stats_free(&test->reported_rate);
	stats_free(&test->reported_time);
	minixtest_stats_free_modules(test->modules);
	coverage_free(&test->coverage);
	faultstats_free(&test->faultstats);
	memset(test, 0, sizeof(*test));
}

static void minixtest_stats_free_tests(struct minixtest_stats_test *tests) {
	struct minixtest_stats_test *next, *test;

	test = tests;
	while (test) {
		next = test->next;
		minixtest_stats_free_test(test);
		FREE(test);
		test = next;
	}
}

void minixtest_stats_free(struct minixtest_stats *stats) {
	assert(stats);

	stats_free(&stats->test);
	minixtest_stats_free_tests(stats->tests);
	minixtest_stats_free_test(&stats->boot);
	minixtest_stats_free_test(&stats->between);
	minixtest_stats_free_test(&stats->alltests);
	minixtest_stats_free_test(&stats->total);
	stats_free(&stats->test_cnt_planned);
	stats_free(&stats->test_cnt_started);
	stats_free(&stats->test_cnt_completed);
	stats_free(&stats->test_cnt_passed);
	stats_free(&stats->test_cnt_failed);
	stats_free(&stats->test_cnt_crashed);
	stats_free(&stats->test_cnt_unreached);
	stats_free(&stats->test_pct_pl_started);
	stats_free(&stats->test_pct_pl_completed);
	stats_free(&stats->test_pct_pl_passed);
	stats_free(&stats->test_pct_pl_failed);
	stats_free(&stats->test_pct_pl_crashed);
	stats_free(&stats->test_pct_pl_unreached);
	stats_free(&stats->test_pct_st_completed);
	stats_free(&stats->test_pct_st_passed);
	stats_free(&stats->test_pct_st_failed);
	stats_free(&stats->test_pct_st_crashed);
	stats_free(&stats->test_pct_cp_passed);
	stats_free(&stats->test_pct_cp_failed);
	memset(stats, 0, sizeof(*stats));
}

size_t minixtest_stats_counttests(const struct minixtest_stats_test *tests) {
	size_t count = 0;
	const struct minixtest_stats_test *test;

	for (test = tests; test; test = test->next) {
		count++;
	}
	return count;
}

int wordcount(const char *s) {
	int count = 0;

	if (!s) return 0;
	for (;;) {
		while (*s == ' ') s++;
		if (!*s) return count;
		count++;
		while (*s && *s != ' ') s++;
	}
}
