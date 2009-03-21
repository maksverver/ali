#ifndef IO_H_INCLUDED
#define IO_H_INCLUDED

#include <stdbool.h>
#include <stdio.h>

/* Simple functions for doing I/O in network byte order. */

int read_int8(FILE *fp);
int read_int16(FILE *fp);
int read_int24(FILE *fp);
int read_int32(FILE *fp);
bool write_int8(FILE *fp, int i);
bool write_int16(FILE *fp, int i);
bool write_int24(FILE *fp, int i);
bool write_int32(FILE *fp, int i);

#endif /* ndef IO_H_INCLUDED */
