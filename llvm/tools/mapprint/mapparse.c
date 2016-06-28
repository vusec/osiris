#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

#include "mapparse.h"

#define MAPFILE_STRING		1 /* followed by string */
#define MAPFILE_FUNCTION	2 /* followed by two string refs */
#define MAPFILE_BASIC_BLOCK	3
#define MAPFILE_INSTRUCTION	4
#define MAPFILE_DINSTRUCTION	5 /* followed by string ref and line num */
#define MAPFILE_FAULT_CANDIDATE	6 /* followed by string ref */
#define MAPFILE_FAULT_INJECTED	7 /* followed by string ref */
#define MAPFILE_MODULE_NAME	8 /* followed by string ref */

typedef unsigned mapfile_stringref_t;

enum mapparse_compatlevel mapparse_compatlevel = mapparse_compat_latest;

struct strings {
	int count;
	int countalloc;
	char **data;
};

static void strings_add(struct strings *strings, char *s)
{
	assert(strings);
	assert(strings->count <= strings->countalloc);

	if (strings->count >= strings->countalloc) {
		strings->countalloc = (strings->count + 1) * 2;
		strings->data = realloc(strings->data, strings->countalloc * sizeof(*strings->data));
		if (!strings->data) {
			perror("error: realloc failed while storing string");
			exit(-1);
		}
	}

	strings->data[strings->count++] = s;
}

static void strings_free(struct strings *strings)
{
	int i;

	assert(strings);

	for (i = 0; i < strings->count; i++) {
		free(strings->data[i]);
	}
	free(strings->data);
}

static char *string_read(FILE *file)
{
	int c, done = 0;
	size_t len = 0, size = 0;
	char *result = NULL;

	do {
		c = getc(file);
		switch (c) {
		case EOF:
			fprintf(stderr, "error: unexpected end of file while reading string\n");
			exit(-1);
		case 0:
			done = 1;
			/* fall through */
		default:
			if (len >= size) {
				size = (len == 0) ? 256 : 2 * len;
				result = realloc(result, size);
				if (!result) {
					perror("error: realloc failed while reading string");
					exit(-1);
				}
			}
			result[len++] = c;
			break;
		}
	} while (!done);

	result = realloc(result, len);
	if (!result) {
		perror("error: realloc failed while shrinking string buffer");
		exit(-1);
	}

	return result;
}

static unsigned long int_read(FILE *file)
{
	int c, shift = 0;
	unsigned long value = 0;

	assert(file);

	do {
		c = getc(file);
		if (c == EOF) {
			fprintf(stderr, "unexpected EOF while reading integer\n");
			exit(-1);
		}
		value |= ((c & 0x7f) << shift);
		shift += 7;
	} while (c & 0x80);

	return value;
}

static mapfile_stringref_t stringref_read(FILE *file)
{
	return int_read(file);
}

static char *stringref_get(mapfile_stringref_t ref, const struct strings *strings)
{
	assert(strings);

	if (ref >= strings->count) {
		fprintf(stderr, "error: invalid string reference %lu (%lu strings available)\n",
			(unsigned long) ref, (unsigned long) strings->count);
		exit(-1);
	}

	return strings->data[ref];
}

static void process_record_string(FILE *file, struct strings *strings)
{
	assert(file);
	assert(strings);

	strings_add(strings, string_read(file));
}

static void process_record_function(
	const struct mapparse_callbacks *callbacks,
	FILE *file,
	const struct strings *strings)
{
	mapfile_stringref_t refName = stringref_read(file);
	const char *name = stringref_get(refName, strings);
	mapfile_stringref_t refRelPath = stringref_read(file);
	const char *relPath = (mapparse_compatlevel == mapparse_compat_hase2014)
		? "" : stringref_get(refRelPath, strings);

	if (callbacks->function) {
		callbacks->function(callbacks->param, name, relPath);
	}
}

static void process_record_basic_block(
	const struct mapparse_callbacks *callbacks)
{
	if (callbacks->basic_block) {
		callbacks->basic_block(callbacks->param);
	}
}

static void process_record_instruction(
	const struct mapparse_callbacks *callbacks)
{
	if (callbacks->instruction) {
		callbacks->instruction(callbacks->param);
	}
}

static void process_record_dinstruction(
	const struct mapparse_callbacks *callbacks,
	FILE *file,
	const struct strings *strings)
{
	mapfile_stringref_t ref = stringref_read(file);
	const char *path = stringref_get(ref, strings);
	mapfile_lineno_t line = int_read(file);

	if (callbacks->dinstruction) {
		callbacks->dinstruction(callbacks->param, path, line);
	}
}

static void process_record_fault_candidate(
	const struct mapparse_callbacks *callbacks,
	FILE *file,
	const struct strings *strings)
{
	mapfile_stringref_t ref = stringref_read(file);
	const char *name = stringref_get(ref, strings);

	if (callbacks->fault_candidate) {
		callbacks->fault_candidate(callbacks->param, name);
	}
}

static void process_record_fault_injected(
	const struct mapparse_callbacks *callbacks,
	FILE *file,
	const struct strings *strings)
{
	mapfile_stringref_t ref = stringref_read(file);
	const char *name = stringref_get(ref, strings);

	if (callbacks->fault_injected) {
		callbacks->fault_injected(callbacks->param, name);
	}
}

static void process_record_module_name(
	const struct mapparse_callbacks *callbacks,
	FILE *file,
	const struct strings *strings)
{
	mapfile_stringref_t ref = stringref_read(file);
	const char *name = stringref_get(ref, strings);

	if (callbacks->module_name) {
		callbacks->module_name(callbacks->param, name);
	}
}

static void process_done(const struct mapparse_callbacks *callbacks)
{
	if (callbacks->done) {
		callbacks->done(callbacks->param);
	}
}

void mapparse_file(
	const struct mapparse_callbacks *callbacks,
	const char *path,
	FILE *file)
{
	int type;
	struct strings strings = { 0, 0, NULL };

	assert(callbacks);
	assert(path);
	assert(file);

	for (;;) {
		type = getc(file);
		switch (type) {
		case MAPFILE_STRING:
			process_record_string(file, &strings);
			break;
		case MAPFILE_FUNCTION:
			process_record_function(callbacks, file, &strings);
			break;
		case MAPFILE_BASIC_BLOCK:
			process_record_basic_block(callbacks);
			break;
		case MAPFILE_INSTRUCTION:
			process_record_instruction(callbacks);
			break;
		case MAPFILE_DINSTRUCTION:
			process_record_dinstruction(callbacks, file, &strings);
			break;
		case MAPFILE_FAULT_CANDIDATE:
			process_record_fault_candidate(callbacks, file, &strings);
			break;
		case MAPFILE_FAULT_INJECTED:
			process_record_fault_injected(callbacks, file, &strings);
			break;
		case MAPFILE_MODULE_NAME:
			process_record_module_name(callbacks, file, &strings);
			break;
		case EOF: goto done;
		default:
			fprintf(stderr, "error: unknown record type %d in file %s at position %ld\n", type, path, ftell(file));
			exit(-1);
		}
	}
done:
	process_done(callbacks);
	strings_free(&strings);
}
