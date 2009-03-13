#ifndef DEBUG_H_INCLUDED
#define DEBUG_H_INCLUDED

#include <stdlib.h>
#include <assert.h>

void info(const char *fmt, ...);
void warn(const char *fmt, ...);
void error(const char *fmt, ...);
__attribute__((__noreturn__))
void fatal(const char *fmt, ...);
void *debug_malloc(size_t size, const char *file, int line);

#define dmalloc(size) debug_malloc(size, __FILE__, __LINE__)

#endif /* ndef DEBUG_H_INCLUDED */
