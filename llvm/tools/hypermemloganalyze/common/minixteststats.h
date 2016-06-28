#ifndef MINIXTESTSTATS_H
#define MINIXTESTSTATS_H

#include <time.h>

#include "coverage.h"
#include "logparse.h"
#include "minixtestlogsummarize.h"

enum run_outcome {
	ro_success,		/* all tests success */
	ro_failed,		/* all tests run, some failed */
	ro_shutdown,		/* controlled shutdown */
	ro_crash,		/* uncontrolled shutdown */
	ro_timeout,		/* incomplete log */
	run_outcome_count
};

struct run_classification {
	enum run_outcome outcome;
	int pretest;
	int recovery;	
};

struct stats {
	int n;
	double sum;
	double sum2;
	size_t valuessize;
	int valuessorted;
	double *values;
};

struct faultstats {
	struct stats faultact;
	struct stats faultactany;
	struct stats faultinj;
	struct stats recoveries[ltckpt_status_count][ltckpt_method_count];
	struct stats recoveryany[ltckpt_status_count][ltckpt_method_count];
};

struct minixtest_stats_module {
	struct minixtest_stats_module *next;
	char *name;
	int count;
	size_t bb_count;
	execcount *bbs;
	int *callsites_closed; /* array of size CALLSITE_ID_MAX, only for total */
};

struct minixtest_stats_test {
	struct minixtest_stats_test *next;
	char *name;
	int count_pass;
	int count_fail;
	int count_crash;
	int count_notreached;
	struct stats duration;
	struct stats reported_count;
	struct stats reported_rate;
	struct stats reported_time;
	struct minixtest_stats_module *modules;
	struct coverage_per_bb coverage;
	struct faultstats faultstats;
};

struct minixtest_stats {
	struct stats test;
	struct minixtest_stats_test *tests;
	struct minixtest_stats_test boot;
	struct minixtest_stats_test between;
	struct minixtest_stats_test alltests;
	struct minixtest_stats_test total;
	/* first index:  enum run_outcome */
	/* second index: 1 = pretest success */
	/* third index:  1 = recovery attempted */
	int classification[run_outcome_count][2][2];

	struct stats test_cnt_planned;
	struct stats test_cnt_started;
	struct stats test_cnt_completed;
	struct stats test_cnt_passed;
	struct stats test_cnt_failed;
	struct stats test_cnt_crashed;
	struct stats test_cnt_unreached;
	struct stats test_pct_pl_started;   /* started   / planned */
	struct stats test_pct_pl_completed; /* completed / planned */
	struct stats test_pct_pl_passed;    /* passed    / planned */
	struct stats test_pct_pl_failed;    /* failed    / planned */
	struct stats test_pct_pl_crashed;   /* crashed   / planned */
	struct stats test_pct_pl_unreached; /* unreached / planned */
	struct stats test_pct_st_completed; /* completed / started */
	struct stats test_pct_st_passed;    /* passed    / started */
	struct stats test_pct_st_failed;    /* failed    / started */
	struct stats test_pct_st_crashed;   /* crashed   / started */
	struct stats test_pct_cp_passed;    /* passed    / completed */
	struct stats test_pct_cp_failed;    /* failed    / completed */
};

void stats_free(struct stats *stats);
double stats_max(struct stats *stats);
double stats_min(struct stats *stats);
double stats_mean(const struct stats *stats);
double stats_mean_trunc(struct stats *stats, int remove_low, int remove_high);
double stats_mean_unixbench(struct stats *stats);
double stats_median(struct stats *stats);
double stats_percentile(struct stats *stats, double percentile);
double stats_stderr(const struct stats *stats);
double stats_stdev(const struct stats *stats);
double stats_var(const struct stats *stats);

void minixtest_stats_add(
	struct minixtest_stats *stats,
	struct run_classification *classification,
	const struct minixtest_log_summary *summary,
	int pretest_count,
	int naive_mode);
const struct minixtest_stats_test *minixtest_stats_find_test(
	const struct minixtest_stats *stats,
	const char *testname);
void minixtest_stats_init_coverage(
	struct minixtest_stats *stats,
	const struct function_hashtable *functions);
const struct minixtest_stats_module *minixtest_stats_find_module(
	const struct minixtest_stats_test *test,
	const char *name);
void minixtest_stats_free(struct minixtest_stats *stats);
size_t minixtest_stats_counttests(const struct minixtest_stats_test *tests);
int wordcount(const char *s);

#endif
