#ifndef HELPER_H
#define HELPER_H

#include <sys/time.h>

#define ASPRINTF(fmt, ...) (asprintf_checked(__FILE__, __LINE__, (fmt), __VA_ARGS__))
#define CALLOC(count, type) ((type *) calloc_checked((count), sizeof(type), #type, __FILE__, __LINE__))
#ifdef DEBUG_MEMORY
#define CHECKPTR(ptr) (checkptr((ptr), __FILE__, __LINE__))
#else
#define CHECKPTR(ptr) do { } while(0)
#endif
#define CLOSE(ptr) (close_checked((ptr), __FILE__, __LINE__))
#define FREE(ptr) (free_checked((ptr), __FILE__, __LINE__))
#define MALLOC(count, type) ((type *) malloc_checked((count), sizeof(type), #type, __FILE__, __LINE__))
#define REALLOC(ptr, count, type) ((type *) realloc_checked((ptr), (count), sizeof(type), #type, __FILE__, __LINE__))
#define STRDUP(str) (strdup_checked((str), __FILE__, __LINE__))
#define STRNDUP(str, n) (strndup_checked((str), (n), __FILE__, __LINE__))

struct string_ll {
	struct string_ll *next;
	const char *str;
};

char *asprintf_checked(const char *file, int line, const char *fmt, ...)
	__attribute__ ((format(printf, 3, 4)));
const char *basename_const(const char *s);
void *calloc_checked(size_t count, size_t size, const char *type,
	const char *file, int line);
#ifdef DEBUG_MEMORY
void checkptr(const void *ptr, const char *file, int line);
#endif
void close_checked(int fd, const char *file, int line);
int ends_with(const char *s, const char *substr, int ignore_case);
void free_checked(void *ptr, const char *file, int line);
unsigned long hashstr(const char *s);
void *malloc_checked(size_t count, size_t size, const char *type,
	const char *file, int line);
void *realloc_checked(void *ptr, size_t count, size_t size, const char *type,
	const char *file, int line);
int safestrcmp(const char *s1, const char *s2);
char *strdup_checked(const char *str, const char *file, int line);
void string_ll_add(struct string_ll **ll_p, const char *s);
int string_ll_find(const struct string_ll *ll, const char *s);
char *strndup_checked(const char *str, size_t n, const char *file, int line);
struct timeval gettimeofday_checked(void);
void timeval_add(struct timeval *dst, const struct timeval *src);
int timeval_compare(const struct timeval *src1, const struct timeval *src2);
int timeval_is_zero(const struct timeval *src);
void timeval_set_to_max(struct timeval *dst, const struct timeval *src);
void timeval_set_to_min(struct timeval *dst, const struct timeval *src);
double timeval_seconds(const struct timeval *src);
void timeval_subtract(struct timeval *dst, const struct timeval *src);

#endif
