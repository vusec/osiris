#ifndef MINIXTESTLOGSUMMARIZE_H
#define MINIXTESTLOGSUMMARIZE_H

#include <stdint.h>
#include <time.h>

#include "logparse.h"

#define CALLSITE_ID_MAX 2048

enum minixtest_log_status {
	mtls_none,
	mtls_start,
	mtls_boot,
	mtls_tests_starting,
	mtls_test_running,
	mtls_test_running_stats,
	mtls_test_done_stats,
	mtls_test_done,
	mtls_tests_complete,
	mtls_quit,
};

struct minixtest_log_summary_recoveries {
	int count[ltckpt_status_count][ltckpt_method_count];
};

struct minixtest_log_summary_test_module {
	struct minixtest_log_summary_test_module *next;
	char *name;
	size_t bb_count;
	execcount *bbs_before;
	execcount *bbs_after;
	execcount *bbs_during;
	execcount faultact;
	struct minixtest_log_summary_recoveries recoveries;
};

struct minixtest_log_summary_test {
	struct minixtest_log_summary_test *next;
	char *name;
	enum minix_test_status status;
	struct timeval timestamp_start;
	struct timeval timestamp_end;
	double reported_count;
	double reported_rate;
	double reported_time;
	struct minixtest_log_summary_test_module *modules;
	struct minixtest_log_summary_test_module **module_last_p;
};

struct minixtest_log_summary_module {
	struct minixtest_log_summary_module *next;
	char *name;
	int callsites_closed[CALLSITE_ID_MAX];
	int faultindexretrieved;
	long faultindex;
	execcount faultact;
	struct minixtest_log_summary_recoveries recoveries;
};

struct minixtest_log_summary {
	char *logpath;
	char *faultspec;
	const struct string_ll *excludemod;
	struct minixtest_log_summary_module *modules;
	struct minixtest_log_summary_module **module_last_p;
	long signal;
	enum minixtest_log_status status;
	enum minixtest_log_status statusfaultfirst;
	enum minixtest_log_status statusfaultlast;
	int status_exited;
	int status_reset;
	struct timeval timestamp_start;
	struct timeval timestamp_boot;
	struct timeval timestamp_tests_starting;
	struct timeval timestamp_tests_complete;
	struct timeval timestamp_exit;
	char *testsplanned;
	struct minixtest_log_summary_test *tests;
	struct minixtest_log_summary_test *test_current;
	struct minixtest_log_summary_test **test_last_p;
};

struct string_ll;
void minixtest_log_summarize(
	const char *logpath,
	const struct string_ll *excludemod,
	struct minixtest_log_summary *summary);
void minixtest_log_summary_free(struct minixtest_log_summary *summary);
const char *minixtest_log_status_to_str(enum minixtest_log_status status);
void recoveries_add(
	struct minixtest_log_summary_recoveries *dst,
	const struct minixtest_log_summary_recoveries *src,
	int factor);

#endif
