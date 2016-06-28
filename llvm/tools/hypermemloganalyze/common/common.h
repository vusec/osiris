#ifndef COMMON_H
#define COMMON_H

typedef unsigned long long execcount;

struct linepath {
	char *path;
	char *pathFixed;
	int line;
	unsigned long hash;
};

#endif
