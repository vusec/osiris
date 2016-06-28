#ifndef DEBUG_H
#define DEBUG_H

#if DEBUGLEVEL >= 1

#include <stdio.h>
#define dbgprintf(args...)  fprintf(stderr, args);

#else

#define dbgprintf(args...)

#endif

#if DEBUGLEVEL >= 2

#include <stdio.h>
#define dbgprintf_v(args...)  fprintf(stderr, args);

#else

#define dbgprintf_v(args...)

#endif

#endif
