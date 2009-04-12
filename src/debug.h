#ifndef DEBUG_H_INCLUDED
#define DEBUG_H_INCLUDED

#include <stdlib.h>
#include <assert.h>

#define info debug_info
#define warn debug_warn
#define error debug_error
#define fatal debug_fatal

void debug_info(const char *fmt, ...);
void debug_warn(const char *fmt, ...);
void debug_error(const char *fmt, ...);
__attribute__((__noreturn__))
void debug_fatal(const char *fmt, ...);
void *debug_malloc(size_t size, const char *file, int line);

#define dmalloc(size) debug_malloc(size, __FILE__, __LINE__)

#endif /* ndef DEBUG_H_INCLUDED */
