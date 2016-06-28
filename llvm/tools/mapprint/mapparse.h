#ifndef _MAPPARSE_H_
#define _MAPPARSE_H_

#include <stdio.h>

typedef unsigned mapfile_lineno_t;

struct mapparse_callbacks {
	void (* basic_block)(void *param);
	void (* dinstruction)(void *param, const char *path, mapfile_lineno_t line);
	void (* done)(void *param);
	void (* fault_candidate)(void *param, const char *name);
	void (* fault_injected)(void *param, const char *name);
	void (* function)(void *param, const char *name, const char *relPath);
	void (* instruction)(void *param);
	void (* module_name)(void *param, const char *name);
	void *param;
};

extern enum mapparse_compatlevel {
	mapparse_compat_latest,
	mapparse_compat_hase2014,
} mapparse_compatlevel;

void mapparse_file(
	const struct mapparse_callbacks *callbacks,
	const char *path,
	FILE *file);

#endif /* !defined(_MAPPARSE_H_) */
