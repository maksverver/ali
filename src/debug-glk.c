#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <glk.h>

static void debug_write(const char *prefix, const char *fmt, va_list ap)
{
    char buffer[4096];
    size_t prefix_len = strlen(prefix);

    if (prefix_len >= sizeof(buffer))
        prefix_len = sizeof(buffer) - 1;
    strcpy(buffer, prefix);
    vsnprintf(buffer + prefix_len, sizeof(buffer) - prefix_len, fmt, ap);	
    glk_set_style(style_Alert);
    glk_put_string(buffer);
    glk_set_style(style_Normal);
}

void debug_info(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    debug_write("[info] ", fmt, ap);
    va_end(ap);	
}

void debug_warn(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    debug_write("[warn] ", fmt, ap);
    va_end(ap);	
}
void debug_error(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    debug_write("[error] ", fmt, ap);
    va_end(ap);	
}

void debug_fatal(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    debug_write("[fatal error] ", fmt, ap);
    va_end(ap);	
    
    glk_exit();
    exit(1);
}
