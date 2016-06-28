#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "coverage.h"
#include "debug.h"
#include "function.h"
#include "helper.h"
#include "minixtestlogsummarize.h"
#include "minixteststats.h"
#include "module.h"

struct list_onetest_test {
	char *name;
};

struct list_onetest_fault {
	struct list_onetest_fault *next;
	char *faultspec;
	size_t runtestcount;
	const struct minixtest_stats_test **runtests;
};

static struct list_onetest_fault *list_onetest_faults;

static struct module_ll *modules;

static struct minixtest_stats stats;

static struct string_ll *setting_extratest;

static int setting_excludeboot;

static struct string_ll *setting_excludetest;

static int setting_multitest = 0;

static int setting_onetest = 0;

static int setting_posttest = 0;

static int setting_pretest = 0;

static int setting_printfaultspec = 0;

static void load_log(const char *logpath) {
	struct run_classification classification;
	struct minixtest_log_summary summary;

	dbgprintf("loading log file %s\n", logpath);
	minixtest_log_summarize(logpath, NULL, &summary);
	minixtest_stats_add(&stats, &classification, &summary, 0, 0);
	minixtest_log_summary_free(&summary);
}

static void load_map(const char *mappath) {
	dbgprintf("loading map file %s\n", mappath);
	module_load_from_map_ll(mappath, &modules);
}

static int faultspec_to_bbindex(const char *faultspec) {
	int bbindex = -1;
	int bbindex_glob, bbindex_mod;
	const struct module *module;
	const char *name;
	ptrdiff_t namelen;
	const char *p = faultspec;

	if (!*faultspec) return -1;

	for (;;) {
		/* parse module name */
		name = p;
		while (*p && *p != ':') p++;
		namelen = p - name;
		if (namelen <= 0 || *p != ':') goto fail;
		p++;

		/* parse bb index */
		bbindex_mod = 0;
		while (*p >= '0' && *p <= '9') {
			bbindex_mod = bbindex_mod * 10 + (*p - '0');
			p++;
		}
		if (bbindex_mod <= 0) goto fail;

		/* look up global bb index; note that bbindex_mod is 1-nased */
		module = module_find_with_namelen(modules, name, namelen);
		if (!module) {
			fprintf(stderr, "warning: cannot find module %.*s in faultspec\n",
				(int) namelen, name);
			continue;
		}

		bbindex_glob = module_bbindex_to_global(module, bbindex_mod - 1);
		if (bbindex_glob < 0) {
			fprintf(stderr, "warning: cannot resolve faultspec "
				"basic block index %.*s:%d\n",
				(int) namelen, name, bbindex_mod);
			continue;
		}

		if (bbindex < 0) {
			bbindex = bbindex_glob;
		} else if (bbindex != bbindex_glob) {
			fprintf(stderr, "warning: inconsistent basic blocks "
				"in faultspec: %s\n", faultspec);
			return -1;
		}

		if (!*p) break;
		if (*p != ':') goto fail;
		p++;
	}
	return bbindex;

fail:
	fprintf(stderr, "warning: incorrect faultspec: %s\n", faultspec);
	return -1;
}

double stats_z2value(const struct stats *stats, double z) {
	return stats_mean(stats) + z * stats_stdev(stats);
}

struct test_list_entry {
	const struct minixtest_stats_test *test;
	double runtime;
	int covered;
	int excluded;
	int order;
	int selected;
};

static int test_list_build_compare(const void *p1, const void *p2) {
	const struct test_list_entry *entry1 = p1;
	const struct test_list_entry *entry2 = p2;

	if (entry1->order < entry2->order) return -1;
	if (entry1->order > entry2->order) return 1;
	return 0;
}

static void test_list_build(
	struct test_list_entry **tests_p,
	size_t *testcount_p,
	int bbindex,
	double runtimestdev) {
	struct test_list_entry *entry;
	const struct minixtest_stats_test *test;
	struct test_list_entry *tests;
	size_t testcount, testindex;

	assert(tests_p);
	assert(testcount_p);

	/* if requested, avoid boot-time faults */
	if (setting_excludeboot && stats.boot.coverage.bbs[bbindex] > 0) {
		dbgprintf("skipping boot-time fault\n");
		*testcount_p = 0;
		*tests_p = NULL;
		return;
	}

	/* allocate list of tests */
	testcount = *testcount_p = minixtest_stats_counttests(stats.tests);
	tests = *tests_p = CALLOC(testcount, struct test_list_entry);

	/* fill list of tests */
	testindex = 0;
	dbgprintf("list of known tests\n");
	for (test = stats.tests; test; test = test->next) {
		entry = &tests[testindex++];
		entry->test = test;
		entry->runtime = stats_z2value(&test->duration, runtimestdev);
		entry->covered = test->coverage.bbs[bbindex] > 0;
		entry->excluded = string_ll_find(setting_excludetest, test->name);
		entry->order = rand();
		dbgprintf("  test %s covered=%d runtime=%.1fs\n",
			test->name, entry->covered, entry->runtime);
	}
	assert(testindex == testcount);

	/* shuffle by sorting on random key */
	qsort(tests, testcount, sizeof(*tests), test_list_build_compare);
}

static int test_list_select_one(
	struct test_list_entry *tests,
	size_t testcount,
	double *runtime_p,
	int covered) {
	int i;
	int retry;
	double runtime = *runtime_p;
	int runtimefactor;
	int selectedmax = 0;
	struct test_list_entry *test;

	assert(tests || testcount == 0);
	assert(runtime_p);
	assert(covered == 0 || covered == 1);

	if (covered) {
		runtimefactor = 1;
	} else {
		runtimefactor = setting_pretest + setting_posttest;
		assert(runtimefactor > 0);
	}

	/* select one test and adjust the remaining runtime accordingly */
	do {
		retry = 0;
		for (i = 0; i < testcount; i++) {
			test = &tests[i];
			if (test->excluded) continue;
			if (test->covered != covered) continue;
			if (test->runtime * runtimefactor > runtime) continue;
			if (test->selected > selectedmax) {
				if (setting_multitest) retry = 1;
				continue;
			}
			*runtime_p = runtime - test->runtime * runtimefactor;
			test->selected++;
			dbgprintf("test %s selected covered=%d\n",
				test->test->name, covered);
			return 1;
		}
		selectedmax++;
	} while (retry);
	return 0;
}

static void test_list_select(
	struct test_list_entry *tests,
	size_t testcount,
	double runtime,
	int *selected_p) {
	int uncovered = setting_pretest || setting_posttest;

	assert(tests || testcount == 0);

	dbgprintf("runtime before selecting tests: %.1fs\n", runtime);

	/* require at least one covered test */
	if (!test_list_select_one(tests, testcount, &runtime, 1)) return;
	*selected_p = 1;

	/* alternately select covered and uncovered tests until runtime runs out */
	while ((uncovered && test_list_select_one(tests, testcount, &runtime, 0)) ||
		test_list_select_one(tests, testcount, &runtime, 1)) {
	}

	dbgprintf("runtime after selecting tests: %.1fs\n", runtime);
}

static void test_list_print(
	const struct test_list_entry *tests,
	size_t testcount,
	const char *faultspec) {
	int covered, first = 1, i, retry, selected, step;
	const struct test_list_entry *test;

	/* sequence:
	 * - uncovered tests (pre-test)
	 * - covered tests (workload)
	 * - covered tests again (workload)
	 * - uncovered tests again (post-test)
	 */
	for (step = 0; step < 3; step++) {
		if (step == 0 && !setting_pretest) continue;
		if (step == 2 && !setting_posttest) continue;
		covered = step == 1;
		selected = 1;
		do {
			retry = 0;
			for (i = 0; i < testcount; i++) {
				test = &tests[i];
				if (test->selected < selected) continue;
				if (test->covered != covered) continue;
				if (first) {
					first = 0;
					if (setting_printfaultspec) {
						printf("%s\t", faultspec);
					}
				} else {
					printf(" ");
				}
				printf("%s", test->test->name);
				if (test->selected > selected) retry = 1;
			}
			selected++;
		} while(retry);
	}
	if (!first || !setting_printfaultspec) {
		printf("\n");
	}
}

static size_t test_list_select_onetest_iterate(
	struct test_list_entry *tests,
	size_t testcount,
	const struct minixtest_stats_test **runtests) {
	size_t count = 0;
	int i;
	struct test_list_entry *test;
	
	for (i = 0; i < testcount; i++) {
		test = &tests[i];
		if (!test->covered) continue;
		if (test->excluded) continue;
		if (runtests) runtests[count] = test->test;
		count++;
	}
	return count;
}

static void test_list_select_onetest(
	struct test_list_entry *tests,
	size_t testcount,
	int *selected_p,
	const char *faultspec) {
	struct list_onetest_fault *fault = CALLOC(1, struct list_onetest_fault);

	/* add fault to linked list */
	fault->next = list_onetest_faults;
	fault->faultspec = STRDUP(faultspec);
	list_onetest_faults = fault;

	/* count tests */
	fault->runtestcount = test_list_select_onetest_iterate(
		tests, testcount, NULL);

	/* list tests */
	fault->runtests = CALLOC(fault->runtestcount,
		const struct minixtest_stats_test *);
	test_list_select_onetest_iterate(
		tests, testcount, fault->runtests);

	if (fault->runtestcount > 0) *selected_p = 1;
}

static void select_test_with_faultspec(
	const char *faultspec,
	double runtime,
	double runtimestdev,
	int *selected_p) {
	int bbindex;
	const struct minixtest_stats_test *test;
	struct string_ll *testname;
	struct test_list_entry *tests;
	size_t testcount;

	dbgprintf("selecting tests for faultspec %s\n", faultspec);

	/* look up which global basic block this faultspec refers to */
	bbindex = faultspec_to_bbindex(faultspec);
	if (bbindex < 0) {
		dbgprintf("bb index not found for faultspec %s\n", faultspec);
		printf("\n");
		return;
	}
	dbgprintf("bb index %d for faultspec %s\n", bbindex, faultspec);

	/* make a list of tests */
	test_list_build(&tests, &testcount, bbindex, runtimestdev);

	/* adjust available for boot time and time between tests */
	runtime -= stats_z2value(&stats.boot.duration, runtimestdev);
	runtime -= stats_z2value(&stats.between.duration, runtimestdev);
	for (testname = setting_extratest; testname; testname = testname->next) {
		test = minixtest_stats_find_test(&stats, testname->str);
		if (!test) {
			fprintf(stderr, "warning: extra test %s not "
				"in golden run\n", testname->str);
			continue;
		}
		runtime -= stats_z2value(&test->duration, runtimestdev);
	}
	if (runtime <= 0) {
		fprintf(stderr, "error: not enough runtime after "
			"boot+extra tests: %.1fs\n", runtime);
		exit(1);
	}

	if (setting_onetest) {
		/* select and print one test at a time */
		test_list_select_onetest(tests, testcount,
			selected_p, faultspec);
	} else {
		/* select a suitable number of tests */
		test_list_select(tests, testcount, runtime, selected_p);

		/* print a list of selected tests, uncovered ones first (as pre-test) */
		test_list_print(tests, testcount, faultspec);
	}

	if (tests) FREE(tests);
}

static void select_tests_with_faultfile(
	FILE *faultfile,
	double runtime,
	double runtimestdev,
	int *selected_p) {
	int c;
	int comment = 0;
	char faultspec[1024];
	size_t faultspeclen = 0;

	assert(faultfile);

	/* parse the file line by line; ech line has any number of
	 * modulename:bbindex pairs separated by colons, optionally followed by
	 * and @-sign and a comment. The specified bbindex is 1-based.
	 */
	for (;;) {
		c = fgetc(faultfile);
		if (c == EOF || c == '\n') {
			if (c == EOF && faultspeclen == 0) break;
			faultspec[faultspeclen] = 0;
			select_test_with_faultspec(faultspec, runtime, runtimestdev, selected_p);
			if (c == EOF) break;
			faultspeclen = 0;
			comment = 0;
			continue;
		}
		if (comment) continue;
		if (c == '@') {
			comment = 1;
			continue;
		}
		if (faultspeclen + 1 >= sizeof(faultspec)) {
			fprintf(stderr, "error: faultspec too long\n");
			return;
		}
		faultspec[faultspeclen++] = c;
	}
}

static void select_tests(
	const char *faultpath,
	double runtime,
	double runtimestdev,
	int *selected_p) {
	FILE *file;

	assert(faultpath);

	file = fopen(faultpath, "r");
	if (!file) {
		fprintf(stderr, "error: could not open fault file %s: %s\n",
			faultpath, strerror(errno));
		return;
	}
	select_tests_with_faultfile(file, runtime, runtimestdev, selected_p);
	fclose(file);
}

static void print_tests_onetest(void) {
	int done, index;
	const struct list_onetest_fault *fault;

	index = 0;
	do {
		done = 1;
		for (fault = list_onetest_faults; fault; fault = fault->next) {
			if (index >= fault->runtestcount) continue;

			if (setting_printfaultspec) {
				printf("%s\t", fault->faultspec);
			}
			printf("%1$s %1$s\n", fault->runtests[index]->name);
			done = 0;
		}
		index++;
	} while (!done);
}

static void list_onetest_free(void) {
	struct list_onetest_fault *fault, *faultnext;

	fault = list_onetest_faults;
	while (fault) {
		faultnext = fault->next;
		if (fault->faultspec) FREE(fault->faultspec);
		if (fault->runtests) FREE(fault->runtests);
		FREE(fault);
		fault = faultnext;
	}
}

static void usage(const char *msg) {
	printf("%s\n", msg);
	printf("\n");
	printf("usage:\n");
	printf("  testpicker [-bfmopP] [-r runtime] [-R stdev] [-s seed] [-x testname]...\n");
	printf("             [-X testname]... faultpath mappath... logpath...\n");
	printf("\n");
	printf("faultpath indicates where the faultpicker output is stored. Each line\n");
	printf("in this file specifies a colon-separated list of module:bbindex pairs,\n");
	printf("optionally followed by an @-sign and a comment. The output lines match\n");
	printf("match up with lines from this file.\n");
	printf("\n");
	printf("Optional parameters:\n");
	printf("-b           - Don't pick tests for faults triggered during boot\n");
	printf("-f           - Write the faultspec followed by a tab at the start of each line,\n");
	printf("               omitting faultspecs that are not reached by any tests.\n");
	printf("-m           - Allow tests to be run multiple times to fill up available time.\n");
	printf("-o           - Select just one test and run it twice; fault types can be\n");
	printf("               repeated if they have multiple triggering tests.\n");
	printf("-p           - Pretest: run uncovered tests before covered tests.\n");
	printf("-P           - Posttest: run uncovered tests after covered tests\n");
	printf("               (same as pretest if both specified).\n");
	printf("-r runtime   - Total runtime (in seconds) available to boot the system and\n");
	printf("               execute all the tests; default: 14 minutes.\n");
	printf("-R stdev     - How many standard deviations to add to the average runtime\n");
	printf("               to account for variation; default: 2.\n");
	printf("-s seed      - Random seed used to get reproducable results. If not specified,\n");
	printf("               a random seed is derived from the current system time.\n");
	printf("-x testname  - Add extra runtime corresponding to the specified test.\n");
	printf("-X testname  - Exclude (do not select) the test with the specified name.\n");
	exit(1);
}

int main(int argc, char **argv) {
	const char *faultpath, *path;
	struct function_hashtable *functions;
	int r;
	double runtime = 600; /* 10 minutes */
	int runtime_spec = 0;
	double runtimestdev = 1;
	int runtimestdev_spec = 0;
	int seed = 0, seed_spec = 0;
	int selected = 0;
	struct timeval timeval;

	/* handle options */
	while ((r = getopt(argc, argv, "bfmopPr:R:s:x:X:")) >= 0) {
		switch (r) {
		case 'b':
			setting_excludeboot = 1;
			break;
		case 'f':
			setting_printfaultspec = 1;
			break;
		case 'm':
			setting_multitest = 1;
			break;
		case 'o':
			setting_onetest = 1;
			break;
		case 'p':
			setting_pretest = 1;
			break;
		case 'P':
			setting_posttest = 1;
			break;
		case 'r':
			if (runtime_spec) usage("Parameter -r specified multiple times");
			runtime = atof(optarg);
			runtime_spec = 1;
			if (runtime <= 0) usage("Parameter runtime has an invalid value");
			break;
		case 'R':
			if (runtimestdev_spec) usage("Parameter -R specified multiple times");
			runtimestdev = atof(optarg);
			runtimestdev_spec = 1;
			break;
		case 's':
			if (seed_spec) usage("Parameter -s specified multiple times");
			seed = atoi(optarg);
			seed_spec = 1;
			break;
		case 'x':
			string_ll_add(&setting_extratest, optarg);
			break;
		case 'X':
			string_ll_add(&setting_excludetest, optarg);
			break;
		default:
			usage("Unknown flag specified");
			break;
		}
	}

	if (setting_onetest && !setting_printfaultspec) usage("Parameter -o requires -f");
	if (setting_onetest && setting_multitest) usage("Parameter -o cannot be combined with -m");
	if (setting_onetest && setting_pretest) usage("Parameter -o cannot be combined with -p");
	if (setting_onetest && setting_posttest) usage("Parameter -o cannot be combined with -P");
	if (setting_onetest && runtime_spec) usage("Parameter -o cannot be combined with -r");
	if (setting_onetest && runtimestdev_spec) usage("Parameter -o cannot be combined with -R");

	/* read faultpath parameter */
	if (optind >= argc) usage("Parameter faultpath not specified");
	faultpath = argv[optind++];

	/* load map files */
	while (optind < argc) {
		path = argv[optind++];
		if (ends_with(path, ".map", 1)) {
			load_map(path);
		} else {
			load_log(path);
		}
	}
	if (!modules) usage("No map files specified");

	/* further initalization */
	if (!seed_spec) {
		timeval = gettimeofday_checked();
		seed = 1000000 * timeval.tv_sec + timeval.tv_usec;
	}
	dbgprintf("seed=%d\n", seed);
	srand(seed);

	/* make overview of functions */
	functions = functions_build_table(modules);
	minixtest_stats_init_coverage(&stats, functions);

	/* select tests */
	select_tests(faultpath, runtime, runtimestdev, &selected);
	if (!selected) {
		fprintf(stderr, "error: no faults selected\n");
		return -1;
	}

	/* in case of onetest, interleave tests for different faults */
	if (setting_onetest) {
		print_tests_onetest();
	}

	/* clean up */
	list_onetest_free();
	minixtest_stats_free(&stats);
	functions_free(functions);
	modules_free(modules);
	modules = NULL;
	return 0;
}
