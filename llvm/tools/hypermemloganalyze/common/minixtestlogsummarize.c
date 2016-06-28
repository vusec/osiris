#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "helper.h"
#include "minixtestlogsummarize.h"

static execcount *dupbbs(const execcount *bbs, size_t bb_count) {
	execcount *result = MALLOC(bb_count, execcount);
	memcpy(result, bbs, bb_count * sizeof(execcount));
	return result;
}

static int namecmp(const char *name, size_t namesz, const char *other) {
	return (namesz != strlen(other)) || memcmp(name, other, namesz);
}

static int namecmp_startswith(const char *name, size_t namesz, const char *other) {
	size_t othersz = strlen(other);
	return (namesz < othersz) || memcmp(name, other, othersz);
}

#define CHECKSTATUS(state, status) (checkstatus(state, status, __FUNCTION__))
#define CHECKSTATUS2(state, status1, status2) (checkstatus2(state, status1, status2, __FUNCTION__))

const char *minixtest_log_status_to_str(enum minixtest_log_status status) {
#define S(s) case s: return #s
	switch (status) {
	S(mtls_none);
	S(mtls_start);
	S(mtls_boot);
	S(mtls_tests_starting);
	S(mtls_test_running);
	S(mtls_test_running_stats);
	S(mtls_test_done_stats);
	S(mtls_test_done);
	S(mtls_tests_complete);
	S(mtls_quit);
	default: assert(0); return "???";
	}
#undef S
}

static int checkstatus(
	const struct logparse_callback_state *state,
	enum minixtest_log_status status,
	const char *func) {
	struct minixtest_log_summary *summary = state->arg;

	if (summary->status_exited) return 0;
	if (summary->status == status) return 1;

	fprintf(stderr, "warning: reached function %s with parser status %s "
		"on line %d of log file %s, expected status %s\n",
		func, minixtest_log_status_to_str(summary->status), state->lineno,
		state->logpath, minixtest_log_status_to_str(status));
	return 0;
}

static int checkstatus2(
	const struct logparse_callback_state *state,
	enum minixtest_log_status status1,
	enum minixtest_log_status status2,
	const char *func) {
	struct minixtest_log_summary *summary = state->arg;

	if (summary->status_exited) return 0;
	if (summary->status == status1 || summary->status == status2) return 1;

	fprintf(stderr, "warning: reached function %s with parser status %s "
		"on line %d of log file %s, expected status %s or %s\n",
		func, minixtest_log_status_to_str(summary->status), state->lineno,
		state->logpath, minixtest_log_status_to_str(status1), minixtest_log_status_to_str(status2));
	return 0;
}

static void callback_edfi_dump_stats(
	const struct logparse_callback_state *state,
	const char *msg, size_t msgsz) {
	struct minixtest_log_summary *summary = state->arg;

	if (summary->status_reset) return;

	if (namecmp_startswith(msg, msgsz, "test-start-") == 0) {
		if (!CHECKSTATUS(state, mtls_test_running)) return;
		summary->status = mtls_test_running_stats;
		return;
	}

	if (namecmp_startswith(msg, msgsz, "test-done-") == 0) {
		if (!CHECKSTATUS(state, mtls_test_running_stats)) return;
		summary->status = mtls_test_done_stats;
		return;
	}

	fprintf(stderr, "warning: unexpected statistics dump description "
		"%.*s on line %d of log file %s\n",
		(int) msgsz, msg, state->lineno, state->logpath);
}

static struct minixtest_log_summary_test_module *find_or_add_module_stats(
	struct minixtest_log_summary_test *test,
	const char *name, size_t namesz) {
	struct minixtest_log_summary_test_module *module;

	for (module = test->modules; module; module = module->next) {
		if (namecmp(name, namesz, module->name) == 0) {
			return module;
		}
	}

	module = *test->module_last_p =
		CALLOC(1, struct minixtest_log_summary_test_module);
	test->module_last_p = &module->next;
	module->name = STRNDUP(name, namesz);
	return module;
}

static int string_ll_contains(
	const struct string_ll *ll,
	const char *name,
	size_t namesz) {
	const char *p;

	/* no need to truncate name if empty linked list specified */
	if (!ll) return 0;

	/* ignore additional specification after @-sign */
	for (p = name + namesz - 1; p >= name; p--) {
		if (*p == '@') {
			namesz = p - name;
			break;
		}
	}

	/* check linked list entries */
	while (ll) {
		if (namecmp(name, namesz, ll->str) == 0) return 1;
		ll = ll->next;
	}
	return 0;
}

static struct minixtest_log_summary_test_module *add_module_stats_internal(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz,
	size_t bb_count) {
	struct minixtest_log_summary_test_module *module;
	struct minixtest_log_summary *summary = state->arg;
	struct minixtest_log_summary_test *test;

	assert(summary);
	test = summary->test_current;
	assert(test);
	assert(test->module_last_p);
	assert(!*test->module_last_p);

	if (string_ll_contains(summary->excludemod, name, namesz)) return NULL;

	module = find_or_add_module_stats(test, name, namesz);
	if (!module->bb_count) {
		module->bb_count = bb_count;
	} else if (module->bb_count != bb_count) {
		fprintf(stderr, "warning: statistics for module %s "
			"in test %s on line %d of log file %s "
			"with inconsistent bb counts\n", module->name,
			test->name, state->lineno, state->logpath);
		return NULL;
	}
	return module;
}

static void add_module_stats_set_bbs(
	const struct logparse_callback_state *state,
	struct minixtest_log_summary_test_module *module,
	execcount **bbs_p,
	execcount *bbs, size_t bb_count) {

	assert(state);
	assert(module);
	assert(bbs_p);
	assert(bbs);

	if (*bbs_p) {
		fprintf(stderr, "warning: duplicate statistics for module %s "
			"on line %d of log file %s\n", module->name,
			state->lineno, state->logpath);
		return;
	}
	*bbs_p = dupbbs(bbs, bb_count);
}

static void add_module_stats_add_bbs(
	const struct logparse_callback_state *state,
	struct minixtest_log_summary_test_module *module,
	execcount **bbs_p,
	execcount *bbs, size_t bb_count,
	int factor) {
	execcount *bbs_sum;
	size_t i;

	assert(state);
	assert(module);
	assert(bbs_p);
	assert(bbs);
	assert(factor == 1 || factor == -1);

	if (!(bbs_sum = *bbs_p)) {
		bbs_sum = *bbs_p = CALLOC(bb_count, execcount);
	}
	for (i = 0; i < bb_count; i++) {
		bbs_sum[i] += bbs[i] * factor;
	}
}

static void add_module_stats_after(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz,
	execcount *bbs,	size_t bb_count) {
	struct minixtest_log_summary_test_module *module;

	module = add_module_stats_internal(state, name, namesz, bb_count);
	if (!module) return;

	add_module_stats_set_bbs(state, module, &module->bbs_after, bbs, bb_count);
	add_module_stats_add_bbs(state, module, &module->bbs_during, bbs, bb_count, 1);
}

static void add_module_stats_before(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz,
	execcount *bbs,	size_t bb_count) {
	struct minixtest_log_summary_test_module *module;

	module = add_module_stats_internal(state, name, namesz, bb_count);
	if (!module) return;

	add_module_stats_set_bbs(state, module, &module->bbs_before, bbs, bb_count);
	add_module_stats_add_bbs(state, module, &module->bbs_during, bbs, bb_count, -1);
}

static void add_module_stats_during(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz,
	execcount *bbs,	size_t bb_count) {
	struct minixtest_log_summary_test_module *module;

	module = add_module_stats_internal(state, name, namesz, bb_count);
	if (!module) return;

	add_module_stats_add_bbs(state, module, &module->bbs_during, bbs, bb_count, 1);
}

static void callback_edfi_dump_stats_module(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz,
	const char *msg, size_t msgsz,
	execcount *bbs,	size_t bb_count) {
	struct minixtest_log_summary *summary = state->arg;

	if (summary->status_reset) return;

	if (!bbs) {
		fprintf(stderr, "warning: missing execcount specification in "
			"dump description %.*s on line %d of log file %s\n",
			(int) msgsz, msg, state->lineno, state->logpath);
		return;
	}
	
	if (namecmp_startswith(msg, msgsz, "test-start-") == 0) {
		if (!CHECKSTATUS(state, mtls_test_running_stats)) return;
		add_module_stats_before(state, name, namesz, bbs, bb_count);
		return;
	}

	if (namecmp_startswith(msg, msgsz, "test-done-") == 0) {
		if (!CHECKSTATUS(state, mtls_test_done_stats)) return;
		add_module_stats_after(state, name, namesz, bbs, bb_count);
		return;
	}

	if (namecmp(msg, msgsz, "do_sef_fi_init") == 0 ||
		namecmp(msg, msgsz, "hypermem_report_startup") == 0) {
		return;
	}

	if (namecmp(msg, msgsz, "edfi_context_release") == 0) {
		if (!CHECKSTATUS2(state, mtls_boot, mtls_test_running_stats)) return;
		if (summary->status == mtls_boot) return;
		add_module_stats_during(state, name, namesz, bbs, bb_count);
		return;
	}

	fprintf(stderr, "warning: unexpected statistics dump description "
		"%.*s on line %d of log file %s\n",
		(int) msgsz, msg, state->lineno, state->logpath);
}

static struct minixtest_log_summary_module *find_or_add_module(
	struct minixtest_log_summary *summary,
	const char *name, size_t namesz) {
	struct minixtest_log_summary_module *module;

	for (module = summary->modules; module; module = module->next) {
		if (namecmp(name, namesz, module->name) == 0) {
			return module;
		}
	}

	module = *summary->module_last_p =
		CALLOC(1, struct minixtest_log_summary_module);
	summary->module_last_p = &module->next;
	module->name = STRNDUP(name, namesz);
	return module;
}

static void callback_edfi_faultindex_get(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz,
	long bbindex) {
	struct minixtest_log_summary *summary = state->arg;
	struct minixtest_log_summary_module *module;

	if (summary->status_reset) return;

	module = find_or_add_module(summary, name, namesz);
	if (module->faultindexretrieved == 0) {
		module->faultindex = bbindex;
	} else if (module->faultindex != bbindex) {
		fprintf(stderr, "warning: inconsistent fault bb specification "
			" for module %s on line %d of log file %s\n",
			module->name, state->lineno, state->logpath);
	}
	module->faultindexretrieved++;
}

static void callback_fault(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz,
	long bbindex,
	long repeat) {
	struct minixtest_log_summary *summary = state->arg;
	struct minixtest_log_summary_module *module;
	struct minixtest_log_summary_test_module *test_module;

	if (summary->status_reset) return;

	module = find_or_add_module(summary, name, namesz);
	if (module->faultindexretrieved < 1) {
		fprintf(stderr, "warning: fault encountered before fault bb "
			"specification was retrieved "
			" for module %s on line %d of log file %s\n",
			module->name, state->lineno, state->logpath);
	} else if (module->faultindex != bbindex) {
		fprintf(stderr, "warning: fault encountered different than "
			"fault bb specification "
			" for module %s on line %d of log file %s\n",
			module->name, state->lineno, state->logpath);
	}
	module->faultact += repeat;

	if (summary->status == mtls_test_running ||
		summary->status == mtls_test_running_stats) {
		assert(summary->test_current);
		test_module = find_or_add_module_stats(summary->test_current,
			name, namesz);
		test_module->faultact += repeat;
	}

	if (summary->statusfaultfirst == mtls_none) {
		summary->statusfaultfirst = summary->status;
	}
	summary->statusfaultlast = summary->status;
}

static void callback_hypermem_faultspec(
	const struct logparse_callback_state *state,
	const char *faultspec, size_t faultspecsz) {
	struct minixtest_log_summary *summary = state->arg;

	if (!CHECKSTATUS(state, mtls_start)) return;
	if (summary->faultspec) {
		fprintf(stderr, "warning: duplicate faultspec on line %d "
			"of log file %s\n", state->lineno, state->logpath);
		return;
	}
	summary->faultspec = STRNDUP(faultspec, faultspecsz);
}

static void callback_hypermem_logpath(
	const struct logparse_callback_state *state,
	const char *logpath, size_t logpathsz) {
	struct minixtest_log_summary *summary = state->arg;

	if (!CHECKSTATUS(state, mtls_none)) return;
	summary->timestamp_start = state->timestamp;
	summary->status = mtls_start;
}

static void callback_print_ltckpt(
	const struct logparse_callback_state *state,
	enum ltckpt_status status,
	enum ltckpt_method method,
	const char *name, size_t namesz) {
	struct minixtest_log_summary *summary = state->arg;
	struct minixtest_log_summary_module *module;
	struct minixtest_log_summary_test_module *test_module;

	if (summary->status_reset) return;

	module = find_or_add_module(summary, name, namesz);
	module->recoveries.count[status][method] += 1;

	if (summary->status == mtls_test_running ||
		summary->status == mtls_test_running_stats) {
		assert(summary->test_current);
		test_module = find_or_add_module_stats(summary->test_current,
			name, namesz);
		test_module->recoveries.count[status][method] += 1;
	}
}

static void callback_print_ltckpt_closed_ipc_callsites(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz,
	long totaltimes,
	long totalsites,
	const uint64_t *callsites, size_t callsitecount) {
	struct minixtest_log_summary *summary = state->arg;
	struct minixtest_log_summary_module *module;
	size_t i;
	uint64_t id;

	module = find_or_add_module(summary, name, namesz);
	for (i = 0; i < callsitecount; i++) {
		id = callsites[i];
		if (id >= CALLSITE_ID_MAX) {
			fprintf(stderr, "warning: callsite id %llu too high "
				"on line %d of log file %s, "
				"consider increasing CALLSITE_ID_MAX\n",
				(long long) id, state->lineno, state->logpath);
			continue;
		}
		module->callsites_closed[id]++;
	}
}

static struct minixtest_log_summary_test *add_test_before(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz) {
	struct minixtest_log_summary *summary = state->arg;
	struct minixtest_log_summary_test *test;

	assert(!*summary->test_last_p);
	test = summary->test_current = *summary->test_last_p =
		CALLOC(1, struct minixtest_log_summary_test);
	test->name = STRNDUP(name, namesz);
	test->reported_count = NAN;
	test->reported_rate = NAN;
	test->reported_time = NAN;
	test->timestamp_start = state->timestamp;
	test->module_last_p = &test->modules;
	return test;
}

static struct minixtest_log_summary_test *add_test_after(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz) {
	struct minixtest_log_summary *summary = state->arg;
	struct minixtest_log_summary_test *test;

	assert(summary->test_current);
	assert(summary->test_last_p);
	assert(*summary->test_last_p == summary->test_current);
	test = summary->test_current;
	summary->test_last_p = &test->next;

	assert(test->status == mts_running);
	test->timestamp_end = state->timestamp;

	if (namecmp(name, namesz, test->name) != 0) {
		fprintf(stderr, "warning: test %.*s finished on line %d "
			"in log file %s but the test last started "
			"was %s\n", (int) namesz, name, state->lineno,
			state->logpath, test->name);
	}
	return test;
}

static struct minixtest_log_summary_test *get_last_test(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz) {
	struct minixtest_log_summary *summary = state->arg;
	struct minixtest_log_summary_test *test;

	assert(summary->test_current);
	test = summary->test_current;

	if (namecmp(name, namesz, test->name) != 0) {
		fprintf(stderr, "warning: output from test %.*s on line %d "
			"in log file %s but the test last completed "
			"was %s\n", (int) namesz, name, state->lineno,
			state->logpath, test->name);
		return NULL;
	}
	return test;
}

static void callback_print_test(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz,
	enum minix_test_status status) {
	struct minixtest_log_summary *summary = state->arg;
	struct minixtest_log_summary_test *test;

	if (status == mts_running) {
		if (!CHECKSTATUS2(state, mtls_tests_starting, mtls_test_done)) return;
		summary->status = mtls_test_running;
		test = add_test_before(state, name, namesz);
	} else {
		if (!CHECKSTATUS2(state, mtls_test_running, mtls_test_done_stats)) return;
		summary->status = mtls_test_done;
		test = add_test_after(state, name, namesz);
	}
	test->status = status;
}

static int output_skip_str(const char **p_p, const char *pend, const char *s) {
	const char *p;

	assert(p_p);
	assert(*p_p);
	assert(pend);

	p = *p_p;
	for (;;) {
		if (!*s) {
			*p_p = p;
			return 1;
		}
		if (p >= pend) return 0;
		if (*(p++) != *(s++)) return 0;
	}
}

static int output_skip_float(const char **p_p, const char *pend, double *value_p) {
	int count;
	double factor;
	const char *p;
	double value;

	assert(p_p);
	assert(*p_p);
	assert(pend);
	assert(value_p);

	p = *p_p;
	value = *value_p = 0;

	count = 0;
	while (p < pend && *p >= '0' && *p <= '9') {
		count++;
		value = value * 10 + (*(p++) - '0');
	}
	if (count < 1) return 0;

	if (p >= pend || *p != '.') {
		*p_p = p;
		*value_p = value;
		return 1;
	}
	p++;

	count = 0;
	factor = 1;
	while (p < pend && *p >= '0' && *p <= '9') {
		count++;
		factor /= 10;
		value = value + (*(p++) - '0') * factor;
	}
	if (count < 1) return 0;

	*p_p = p;
	*value_p = value;
	return 1;
}

static int output_skip_int(const char **p_p, const char *pend, int *value_p) {
	int count;
	const char *p;
	int value;

	assert(p_p);
	assert(*p_p);
	assert(pend);
	assert(value_p);

	p = *p_p;
	value = *value_p = 0;

	count = 0;
	while (p < pend && *p >= '0' && *p <= '9') {
		count++;
		value = value * 10 + (*(p++) - '0');
	}
	if (count < 1) return 0;

	*p_p = p;
	*value_p = value;
	return 1;
}

static int output_has_char(const char *p, const char *pend, char c) {

	assert(p);
	assert(pend);

	while (p < pend) {
		if (*(p++) == c) return 1;
	}
	return 0;
}

static void callback_print_test_output(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz,
	const char *msg, size_t msgsz) {
	int israte;
	struct minixtest_log_summary_test *test;
	const char *p, *pend;
	double *target;
	double value;

	if (!CHECKSTATUS(state, mtls_test_done)) return;

	test = get_last_test(state, name, namesz);
	if (!test) return;

	p = msg;
	pend = p + msgsz;
	if (output_skip_str(&p, pend, "COUNT|")) {
		/* example: "COUNT|123456.789|1|lps" */
		if (!output_skip_float(&p, pend, &value) ||
			!output_skip_str(&p, pend, "|") ||
			!output_skip_int(&p, pend, &israte) ||
			!output_skip_str(&p, pend, "|") ||
			output_has_char(p, pend, '|')) {
			goto errorparse;
		}
		target = israte ? &test->reported_rate : &test->reported_count;
	} else if (output_skip_str(&p, pend, "TIME|")) {
		/* example: "TIME|123456.789" */
		if (!output_skip_float(&p, pend, &value) ||
			p != pend) {
			goto errorparse;
		}
		target = &test->reported_time;
	} else {
		return;
	}
	if (isnan(*target)) {
		*target = value;
	} else {
		*target += value;
	}
	return;

errorparse:
	fprintf(stderr, "warning: output from test %.*s on line %d in "
		"log file %s cannot be parsed: %.*s\n",
		(int) namesz, name, state->lineno,
		state->logpath, (int) msgsz, msg);
}

static void callback_print_tests_completed(
	const struct logparse_callback_state *state,
	const char *namesbad, size_t namessz) {
	struct minixtest_log_summary *summary = state->arg;

	if (!CHECKSTATUS(state, mtls_test_done)) return;

	summary->timestamp_tests_complete = state->timestamp;
	summary->status = mtls_tests_complete;
}

static void callback_print_tests_starting(
	const struct logparse_callback_state *state,
	const char *names, size_t namessz) {
	struct minixtest_log_summary *summary = state->arg;

	/* if EDFI is disabled we don't know when the system booted,
	 * but this message is the closest approximation
	 */
	if (summary->status == mtls_start) {
		summary->timestamp_boot = state->timestamp;
		summary->status = mtls_boot;
	}

	if (!CHECKSTATUS(state, mtls_boot)) return;

	assert(!summary->testsplanned);

	summary->timestamp_tests_starting = state->timestamp;
	summary->status = mtls_tests_starting;
	summary->testsplanned = STRNDUP(names, namessz);
}

static void callback_print_startup(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz) {
	struct minixtest_log_summary *summary = state->arg;

	if (summary->status_reset) return;

	if (summary->status == mtls_start) {
		summary->timestamp_boot = state->timestamp;
		summary->status = mtls_boot;
	}
}

static void callback_qemu_exit(const struct logparse_callback_state *state) {
	struct minixtest_log_summary *summary = state->arg;

	if (summary->status_reset) return;

	/* allowed in any state due to crashes */
	if (summary->status == mtls_tests_complete) {
		summary->status = mtls_quit;
	}
	if (!summary->status_exited) {
		summary->status_exited = 1;
		summary->timestamp_exit = state->timestamp;
	}
}

static void callback_qemu_hypermem_reset(const struct logparse_callback_state *state) {
	struct minixtest_log_summary *summary = state->arg;

	if (summary->status_reset) return;

	/* allowed in any state due to crashes */
	if (summary->status > mtls_start) {
		summary->status_reset = 1;
		if (!summary->status_exited) {
			summary->status_exited = 1;
			summary->timestamp_exit = state->timestamp;
		}
	}
}

static void callback_qemu_signal(
	const struct logparse_callback_state *state,
	long number) {
	struct minixtest_log_summary *summary = state->arg;

	/* allowed in any state due to timeouts */
	if (number <= 0) {
		fprintf(stderr, "warning: signal number %ld not positive "
			"on line %d in log file %s\n", number, state->lineno,
			state->logpath);
		return;
	}
	if (summary->signal > 0) {
		fprintf(stderr, "warning: second termination signal "
			"on line %d in log file %s\n", state->lineno,
			state->logpath);
		return;
	}
	summary->signal = number;
}

void minixtest_log_summarize(
	const char *logpath,
	const struct string_ll *excludemod,
	struct minixtest_log_summary *summary) {
	struct logparse_callbacks callbacks;
	struct minixtest_log_summary_test *test;
	struct timeval timestamp_last;

	assert(logpath);
	assert(summary);

	memset(summary, 0, sizeof(*summary));
	summary->logpath = STRDUP(logpath);
	summary->excludemod = excludemod;
	summary->module_last_p = &summary->modules;
	summary->test_last_p = &summary->tests;
	
	dbgprintf("loading log file %s\n", logpath);
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.arg = summary;
	callbacks.edfi_dump_stats = callback_edfi_dump_stats;
	callbacks.edfi_dump_stats_module = callback_edfi_dump_stats_module;
	callbacks.edfi_faultindex_get = callback_edfi_faultindex_get;
	callbacks.fault = callback_fault;
	callbacks.hypermem_logpath = callback_hypermem_logpath;
	callbacks.hypermem_faultspec = callback_hypermem_faultspec;
	callbacks.print_ltckpt = callback_print_ltckpt;
	callbacks.print_ltckpt_closed_ipc_callsites = callback_print_ltckpt_closed_ipc_callsites;
	callbacks.print_startup = callback_print_startup;
	callbacks.print_test = callback_print_test;
	callbacks.print_test_output = callback_print_test_output;
	callbacks.print_tests_completed = callback_print_tests_completed;
	callbacks.print_tests_starting = callback_print_tests_starting;
	callbacks.qemu_exit = callback_qemu_exit;
	callbacks.qemu_hypermem_reset = callback_qemu_hypermem_reset;
	callbacks.qemu_signal = callback_qemu_signal;

	logparse_from_path(logpath, &callbacks, &timestamp_last);

	/* ensure all timestamps are filled in, even if MINIX shus down
	 * prematurely or the log is truncated due to a timeout
	 */
	if (!summary->status_exited) {
		fprintf(stderr, "warning: log file %s contains no exit "
			"message, seems truncated\n", logpath);
		summary->timestamp_exit = timestamp_last;
	}
	if (summary->status < mtls_boot) {
		summary->timestamp_boot = summary->timestamp_exit;
	}
	if (summary->status < mtls_tests_starting) {
		summary->timestamp_tests_starting = summary->timestamp_exit;
	}
	if (summary->status < mtls_tests_complete) {
		summary->timestamp_tests_complete = summary->timestamp_exit;
	}
	for (test = summary->tests; test; test = test->next) {
		if (test->status == mts_running) {
			test->timestamp_end = test->next ?
				test->next->timestamp_start :
				summary->timestamp_tests_complete;
		}
	}
}

static void minixtest_log_summary_free_module(struct minixtest_log_summary_module *module) {

	assert(module);

	if (module->name) FREE(module->name);
	memset(module, 0, sizeof(*module));
}

static void minixtest_log_summary_free_modules(struct minixtest_log_summary_module *modules) {
	struct minixtest_log_summary_module *next, *module;

	module = modules;
	while (module) {
		next = module->next;
		minixtest_log_summary_free_module(module);
		FREE(module);
		module = next;
	}
}

static void minixtest_log_summary_free_test_module(struct minixtest_log_summary_test_module *module) {

	assert(module);

	if (module->name) FREE(module->name);
	if (module->bbs_during) FREE(module->bbs_during);
	if (module->bbs_before) FREE(module->bbs_before);
	if (module->bbs_after) FREE(module->bbs_after);
	memset(module, 0, sizeof(*module));
}

static void minixtest_log_summary_free_test_modules(struct minixtest_log_summary_test_module *modules) {
	struct minixtest_log_summary_test_module *next, *module;

	module = modules;
	while (module) {
		next = module->next;
		minixtest_log_summary_free_test_module(module);
		FREE(module);
		module = next;
	}
}

static void minixtest_log_summary_free_test(struct minixtest_log_summary_test *test) {

	assert(test);

	if (test->name) FREE(test->name);
	minixtest_log_summary_free_test_modules(test->modules);
	memset(test, 0, sizeof(*test));
}

static void minixtest_log_summary_free_tests(struct minixtest_log_summary_test *tests) {
	struct minixtest_log_summary_test *next, *test;

	test = tests;
	while (test) {
		next = test->next;
		minixtest_log_summary_free_test(test);
		FREE(test);
		test = next;
	}
}

void minixtest_log_summary_free(struct minixtest_log_summary *summary) {

	assert(summary);

	if (summary->logpath) FREE(summary->logpath);
	if (summary->faultspec) FREE(summary->faultspec);
	minixtest_log_summary_free_modules(summary->modules);
	minixtest_log_summary_free_tests(summary->tests);
	if (summary->testsplanned) FREE(summary->testsplanned);
	memset(summary, 0, sizeof(*summary));
}
