#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "helper.h"

struct meminfo {
	struct meminfo *next;
	void *ptr;
	size_t count;
	size_t size;
	const char *type;
	const char *file;
	int line;
};

#ifdef DEBUG_MEMORY

struct meminfo_summary {
	size_t count_ptr;
	size_t count_type;
	size_t size;
	const char *type;
	const char *file;
	int line;
};

#define HASHTABLE_SIZE 262144
static struct meminfo *hashtable[HASHTABLE_SIZE];

#define SUMMARY_COUNT 4096
static struct meminfo_summary summaries[SUMMARY_COUNT];

static size_t summary_count = 0;

static unsigned hashptr(const void *ptr) {
	return ((unsigned) (intptr_t) ptr >> 4) % HASHTABLE_SIZE;
}

static void meminfo_free_pointers(void) {
	int i;
	struct meminfo *mi;

	/* this frees all pointers allocated, none of them may be references
	 * and the program must be aborted afterwards; it is necessary because
	 * we need to free up memory to build a report
	 */
	for (i = 0; i < HASHTABLE_SIZE; i++) {
		for (mi = hashtable[i]; mi; mi = mi->next) {
			free(mi->ptr);
		}
	}
}

static void meminfo_summarize_add(struct meminfo *mi) {
	int i;
	struct meminfo_summary *summary;

	for (i = 0; i < summary_count; i++) {
		summary = &summaries[i];
		if (summary->size == mi->size &&
			strcmp(summary->type, mi->type) == 0 &&
			strcmp(summary->file, mi->file) == 0 &&
			summary->line == mi->line) {
			summary->count_ptr += 1;
			summary->count_type += mi->count;
			return;
		}
	}

	if (summary_count >= SUMMARY_COUNT) {
		fprintf(stderr, "SUMMARY_COUNT is not enough\n");
		return;
	}

	summary = &summaries[summary_count++];
	summary->count_ptr = 1;
	summary->count_type = mi->count;
	summary->size = mi->size;
	summary->type = mi->type;
	summary->file = mi->file;
	summary->line = mi->line;
}

static void meminfo_summarize(void) {
	int i;
	struct meminfo *mi;

	summary_count = 0;
	for (i = 0; i < HASHTABLE_SIZE; i++) {
		for (mi = hashtable[i]; mi; mi = mi->next) {
			meminfo_summarize_add(mi);
		}
	}
}

static void meminfo_dump(void) {
	int i;
	struct meminfo_summary *summary;

	/* free up some memory; the program is dead after this but the metadata
	 * remains
	 */
	meminfo_free_pointers();

	meminfo_summarize();

	fprintf(stderr, "count_ptr\tcount_type\tsize\ttype\tlocation\n");
	for (i = 0; i < summary_count; i++) {
		summary = &summaries[i];
		fprintf(stderr, "%lu\t%lu\t%lu\t%s\t%s:%d\n",
			(unsigned long) summary->count_ptr,
			(unsigned long) summary->count_type,
			(unsigned long) summary->size, summary->type,
			summary->file, summary->line);
	}

	abort();
}

static struct meminfo **meminfo_find_ptr(const void *ptr) {
	struct meminfo *mi, **mi_p;

	mi_p = &hashtable[hashptr(ptr)];
	while ((mi = *mi_p)) {
		if (mi->ptr == ptr) return mi_p;
		mi_p = &mi->next;
	}
	return mi_p;
}

static struct meminfo *meminfo_find(const void *ptr) {
	return *meminfo_find_ptr(ptr);
}

static void meminfo_register(void *ptr, size_t count, size_t size,
	const char *type, const char *file, int line) {
	struct meminfo *mi, **mi_p;

	assert(type);
	assert(file);

	if (!ptr) return;

	mi_p = meminfo_find_ptr(ptr);
	if ((mi = *mi_p)) {
		fprintf(stderr, "error: pointer %p allocated at %s:%d "
			"previously allocated at %s:%d (there must be "
			"an uninstrumented free)\n",
			ptr, file, line, mi->file, mi->line);
		abort();
	}

	mi = *mi_p = calloc(1, sizeof(struct meminfo));
	if (!mi) {
		fprintf(stderr, "error: meminfo allocation failed\n");
		meminfo_dump();
	}
	mi->ptr = ptr;
	mi->count = count;
	mi->size = size;
	mi->type = type;
	mi->file = file;
	mi->line = line;
}

static void meminfo_unregister(void *ptr, const char *file, int line,
	struct meminfo *meminfo) {
	struct meminfo *mi, **mi_p;

	assert(file);

	if (!ptr) return;

	mi_p = meminfo_find_ptr(ptr);
	if (!(mi = *mi_p)) {
		fprintf(stderr, "error: pointer %p freed at %s:%d not previously "
			"allocated (probably a double free or uninstrumented "
			"allocation)\n", ptr, file, line);
		abort();
	}

	if (meminfo) *meminfo = *mi;
	*mi_p = mi->next;
	free(mi);
}

void checkptr(const void *ptr, const char *file, int line) {
	struct meminfo *mi;

	mi = meminfo_find(ptr);
	if (!mi) {
		fprintf(stderr, "error: pointer %p checked at %s:%d "
			"not previously allocated\n", ptr, file, line);
		abort();
	}
}
#else
#define meminfo_dump()
#define meminfo_register(ptr, count, size, type, file, line)
#define meminfo_unregister(ptr, file, line, meminfo)
#endif

static void alloc_error(size_t count, const char *type,
	const char *file, int line) {
	fprintf(stderr, "error: could not allocate %lu %s objects "
		"at %s:%d: %s\n", (long) count, type, file, line,
		strerror(errno));
	meminfo_dump();
	abort();
}

static size_t alloc_multiply(size_t count, size_t size, const char *type,
	const char *file, int line) {
	size_t result = count * size;
	if (result / size != count) {
		errno = EDOM;
		alloc_error(count, type, file, line);
	}
	return result;
}

char *asprintf_checked(const char *file, int line, const char *fmt, ...) {
	va_list ap;
	char *result;

	va_start(ap, fmt);
	if (vasprintf(&result, fmt, ap) < 0) {
		alloc_error(1, "formatted string", file, line);
	}
	va_end(ap);

	meminfo_register(result, strlen(result) + 1, 1, "char", file, line);
	return result;
}

const char *basename_const(const char *s) {
	const char *result = s;

	if (!s) return NULL;

	while (*s) {
		if (*s == '/') result = s + 1;
		s++;
	}

	return result;
}

void *calloc_checked(size_t count, size_t size, const char *type,
	const char *file, int line) {
	void *ptr;

	if (!count || !size) return NULL;

	ptr = calloc(count, size);
	if (!ptr) alloc_error(count, type, file, line);
	meminfo_register(ptr, count, size, type, file, line);
	return ptr;
}

int ends_with(const char *s, const char *substr, int ignore_case) {
	size_t slen = strlen(s);
	size_t substrlen = strlen(substr);

	if (slen < substrlen) return 0;

	return (ignore_case ? strcasecmp : strcmp)(s + slen - substrlen, substr) == 0;
}

void close_checked(int fd, const char *file, int line) {
	if (close(fd) < 0) {
		fprintf(stderr, "error: close failed at %s:%d: %s\n",
			file, line, strerror(errno));
		abort();
	}
}

void free_checked(void *ptr, const char *file, int line) {
	struct meminfo mi = { };

	meminfo_unregister(ptr, file, line, &mi);
	if (mi.count > 0 || mi.size > 0) {
		memset(ptr, 0xdb, mi.count * mi.size);
	}
	if (!ptr) return;
	free(ptr);
}

unsigned long hashstr(const char *s) {
	unsigned long hash = 0;
	while (*s) {
		hash = hash * 3 + *(s++);
	}
	return hash;
}

void *malloc_checked(size_t count, size_t size, const char *type,
	const char *file, int line) {
	void *ptr;

	if (!count || !size) return NULL;

	ptr = malloc(alloc_multiply(count, size, type, file, line));
	if (!ptr) alloc_error(count, type, file, line);
	meminfo_register(ptr, count, size, type, file, line);
	return ptr;
}

void *realloc_checked(void *ptr, size_t count, size_t size, const char *type,
	const char *file, int line) {

	if (!count || !size) {
		if (ptr) free_checked(ptr, file, line);
		return NULL;
	}

	meminfo_unregister(ptr, file, line, NULL);
	ptr = realloc(ptr, alloc_multiply(count, size, type, file, line));
	if (!ptr) alloc_error(count, type, file, line);
	meminfo_register(ptr, count, size, type, file, line);
	return ptr;
}

int safestrcmp(const char *s1, const char *s2) {
	if (!s1) return s2 ? -1 : 0;
	if (!s2) return 1;
	return strcmp(s1, s2);
}

char *strdup_checked(const char *str, const char *file, int line) {
	char *result;

	if (!str) return NULL;

	result = strdup(str);
	if (!result) alloc_error(strlen(str) + 1, "char", file, line);
	meminfo_register(result, strlen(result) + 1, 1, "char", file, line);
	return result;
}

void string_ll_add(struct string_ll **ll_p, const char *s) {
	struct string_ll *ll;

	assert(ll_p);
	assert(s);

	ll = CALLOC(1, struct string_ll);
	ll->next = *ll_p;
	ll->str = s;
	*ll_p = ll;
}

int string_ll_find(const struct string_ll *ll, const char *s) {

	assert(s);

	while (ll) {
		assert(ll->str);
		if (strcmp(ll->str, s) == 0) return 1;
		ll = ll->next;
	}
	return 0;
}

char *strndup_checked(const char *str, size_t n, const char *file, int line) {
	char *result;

	if (!str) return NULL;

	result = strndup(str, n);
	if (!result) alloc_error(n + 1, "char", file, line);
	meminfo_register(result, strlen(result) + 1, 1, "char", file, line);
	return result;
}

struct timeval gettimeofday_checked(void) {
	struct timeval timeval;

	if (gettimeofday(&timeval, NULL) < 0) {
		perror("error: gettimeofday failed");
		exit(-1);
	}
	return timeval;
}

void timeval_add(struct timeval *dst, const struct timeval *src) {
	dst->tv_sec += src->tv_sec;
	dst->tv_usec += src->tv_usec;
	if (dst->tv_usec >= 1000000) {
		dst->tv_sec++;
		dst->tv_usec -= 1000000;
	}
}

int timeval_compare(const struct timeval *src1, const struct timeval *src2) {
	assert(src1);
	assert(src1->tv_usec >= 0);
	assert(src1->tv_usec < 1000000);
	assert(src2);
	assert(src2->tv_usec >= 0);
	assert(src2->tv_usec < 1000000);
	if (src1->tv_sec > src2->tv_sec) return 1;
	if (src1->tv_sec < src2->tv_sec) return -1;
	if (src1->tv_usec > src2->tv_usec) return 1;
	if (src1->tv_usec < src2->tv_usec) return -1;
	return 0;
}

int timeval_is_zero(const struct timeval *src) {
	assert(src);
	return !src->tv_sec && !src->tv_usec;
}

double timeval_seconds(const struct timeval *src) {
	assert(src);
	return src->tv_sec + src->tv_usec / 1000000.0;
}

void timeval_set_to_max(struct timeval *dst, const struct timeval *src) {
	assert(dst);
	assert(src);
	if (timeval_is_zero(src)) return;
	if (timeval_is_zero(dst) || timeval_compare(dst, src) < 0) *dst = *src;
}

void timeval_set_to_min(struct timeval *dst, const struct timeval *src) {
	assert(dst);
	assert(src);
	if (timeval_is_zero(src)) return;
	if (timeval_is_zero(dst) || timeval_compare(dst, src) > 0) *dst = *src;
}

void timeval_subtract(struct timeval *dst, const struct timeval *src) {
	dst->tv_sec -= src->tv_sec;
	if (dst->tv_usec < src->tv_usec) {
		dst->tv_sec--;
		dst->tv_usec += 1000000;
	}
	dst->tv_usec -= src->tv_usec;
}
