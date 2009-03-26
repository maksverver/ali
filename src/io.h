#ifndef IO_H_INCLUDED
#define IO_H_INCLUDED

#include <stdbool.h>
#include <stdlib.h>

#ifdef WITH_LZMA
#include "lzma/LzmaDec.h"
#endif

typedef enum IOCompression {
    IOC_AUTO,       /* auto-detect a suitable (de)compression format */
    IOC_COPY,       /* copy bytes without (de)compression */
    IOC_LZMA        /* use LZMA (de)compression */
} IOCompression;

typedef enum IOMode {
    IOM_CLOSED,
    IOM_RDONLY,
    IOM_WRONLY
} IOMode;

typedef struct IOStream
{
    void            *fp;                /* underlying file handle */
    IOMode          iom;                /* access mode (read or write?) */
    IOCompression   ioc;                /* (de)compression */

    int             pos_in,  len_in;    /* input buffer position/length */
    int             pos_out, len_out;   /* output buffer position/length */
    unsigned char   buf_in[512];        /* input buffer data */
    unsigned char   buf_out[512];       /* output buffer data */

#ifdef WITH_LZMA
    ELzmaStatus     lzma_status;        /* LZMA (de/en)coder status */
    CLzmaDec        lzma_dec;           /* LZMA decoder */
#endif
} IOStream;


/* Opening/closing IO streams */
bool ios_open(IOStream *ios, const char *path, IOMode iom, IOCompression ioc);
bool ios_eof(IOStream *ios);
void ios_close(IOStream *ios);

/* Read binary data. */
bool read_data(IOStream *ios, void *buf, size_t size);

/* Write binary data. */
bool write_data(IOStream *ios, const void *buf, size_t size);

/* Reading integers in network byte order. */
bool read_int8  (IOStream *ios, int *i);
bool read_int16 (IOStream *ios, int *i);
bool read_int24 (IOStream *ios, int *i);
bool read_int32 (IOStream *ios, int *i);

/* Writing integers in network byte order. */
bool write_int8  (IOStream *ios, int i);
bool write_int16 (IOStream *ios, int i);
bool write_int24 (IOStream *ios, int i);
bool write_int32 (IOStream *ios, int i);

#endif /* ndef IO_H_INCLUDED */
