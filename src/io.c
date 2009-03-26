#include "io.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void *lzma_alloc(void *p, size_t size)
{
    void *res;

    (void)p;
    res = malloc(size);
    assert(res != NULL);

    return res;
}

static void lzma_free(void *p, void *addr)
{
    (void)p;
    free(addr);
}

static ISzAlloc szalloc = { lzma_alloc, lzma_free };

static bool autodetect_lzma(IOStream *ios)
{
    /* Ensure we could read the LZMA stream header */
    if (ios->len_in < LZMA_PROPS_SIZE + 8)
        return false;

    /* Check if our LZMA properties are sensible. */
    CLzmaProps props;
    if (LzmaProps_Decode(&props, ios->buf_in, LZMA_PROPS_SIZE) != SZ_OK)
        return false;
    if ((props.dicSize&(props.dicSize - 1)) != 0)
        return false;

    /* Try to construct LZMA decoder */
    LzmaDec_Construct(&ios->lzma_dec);
    if (LzmaDec_Allocate(&ios->lzma_dec, ios->buf_in, LZMA_PROPS_SIZE, &szalloc)
        != SZ_OK) return false;
    LzmaDec_Init(&ios->lzma_dec);

    /* Everything seems OK so far -- asume stream is LZMA encoded. */
    ios->ioc    = IOC_LZMA;
    ios->pos_in = LZMA_PROPS_SIZE + 8;
    return true;
}

bool ios_open(IOStream *ios, const char *path, IOMode iom, IOCompression ioc)
{
    if (iom == IOM_RDONLY)
    {
        ios->fp  = fopen(path, "rb");
        if (ios->fp == NULL)
            return false;
        ios->iom = IOM_RDONLY;
        ios->ioc = IOC_COPY;
        ios->pos_in  = ios->len_in  = 0;
        ios->pos_out = ios->len_out = 0;

        if (ioc == IOC_LZMA || ioc == IOC_AUTO)
        {
            ios->len_in = fread(ios->buf_in, 1, sizeof(ios->buf_in), ios->fp);
            if (!autodetect_lzma(ios))
            {
                if (ioc != IOC_AUTO)
                    return false;

                /* Falling back to copy mode; move data to output buffer. */
                memcpy(ios->buf_out, ios->buf_in, ios->len_in);
                ios->len_out = ios->len_in;
                ios->len_in  = 0;
            }
        }
        return true;
    }

    if (iom == IOM_WRONLY)
    {
        if (!(ioc == IOC_COPY || ioc == IOC_AUTO))
            return false;
        ios->fp  = fopen(path, "wb");
        if (ios->fp == NULL)
            return false;
        ios->iom = IOM_WRONLY;
        ios->ioc = IOC_COPY;
        ios->pos_in  = ios->len_in  = 0;
        ios->pos_out = ios->len_out = 0;
        return true;
    }

    return false;
}

bool ios_eof(IOStream *ios)
{
    return ios->pos_in  == ios->len_in &&
           ios->pos_out == ios->len_out && feof(ios->fp);
}

void ios_close(IOStream *ios)
{
    if (ios->iom == IOM_RDONLY && ios->ioc == IOC_LZMA)
        LzmaDec_Free(&ios->lzma_dec, &szalloc);
    fclose(ios->fp);
    ios->fp = NULL;
}

static bool refill_input(IOStream *ios)
{
    /* when called, buf_out is empty and must be refilled */

    if (ios->ioc == IOC_COPY)
    {
        /* Read directly into output buffer */
        size_t nread = fread(ios->buf_out, 1, sizeof(ios->buf_out), ios->fp);
        if (nread == 0)
            return false;
        ios->pos_out = 0;
        ios->len_out = (int)nread;
        return true;
    }
    else
    if (ios->ioc == IOC_LZMA)
    {
        ios->pos_out = ios->len_out = 0;

        while ((size_t)ios->len_out < sizeof(ios->buf_out))
        {
            if (ios->pos_in == ios->len_in)
            {
                /* Need to reed new input data */
                size_t nread = fread(ios->buf_in, 1, sizeof(ios->buf_in), ios->fp);
                ios->pos_in = 0;
                ios->len_in = nread;
                if (nread == 0)
                    break;
            }

            /* Decode some input */
            size_t avail_in  = ios->len_in - ios->pos_in;
            size_t avail_out = sizeof(ios->buf_out) - ios->len_out;
            SRes res = LzmaDec_DecodeToBuf( &ios->lzma_dec,
                ios->buf_out + ios->len_out, &avail_out,
                ios->buf_in  + ios->pos_in,  &avail_in,
                LZMA_FINISH_ANY, &ios->lzma_status );
            ios->len_out += avail_out;
            ios->pos_in  += avail_in;
            if (res != SZ_OK)
                return false;
        }

        return ios->len_out > 0;
    }
    else
    {
        return false;
    }
}

bool read_data(IOStream *ios, void *buf, size_t size)
{
    unsigned char *p = buf, *q = buf + size;
    while (p != q)
    {
        while (ios->pos_out < ios->len_out && p != q)
            *p++ = ios->buf_out[ios->pos_out++];

        if (p != q && !refill_input(ios))
                return false;
    }

    return true;
}

bool write_data(IOStream *ios, const void *buf, size_t size)
{
    /* Only direct copying is supported right now */
    return fwrite(buf, 1, size, ios->fp) == size;
}

bool read_int8(IOStream *ios, int *i)
{
    signed char buf[1];
    if (!read_data(ios, buf, sizeof(buf)))
        return false;
    if (i != NULL)
        *i = buf[0];
    return true;
}

bool read_int16(IOStream *ios, int *i)
{
    unsigned char buf[2];
    if (!read_data(ios, buf, sizeof(buf)))
        return false;
    if (i != NULL)
        *i = ((signed char)buf[0] << 8) | buf[1];
    return true;
}

bool read_int24(IOStream *ios, int *i)
{
    unsigned char buf[3];
    if (!read_data(ios, buf, sizeof(buf)))
        return false;
    if (i != NULL)
        *i = ((signed char)buf[0] << 16) | (buf[1] << 8) | buf[2];
    return true;
}

bool read_int32(IOStream *ios, int *i)
{
    unsigned char buf[4];
    if (!read_data(ios, buf, sizeof(buf)))
        return false;
    if (i != NULL)
    {
        *i = ((signed char)buf[0] << 24) | (buf[1] << 16) |
             (buf[2] << 8) | buf[3];
    }
    return true;
}

bool write_int8(IOStream *ios, int i)
{
    unsigned char bytes[1] = { i&255 };
    return write_data(ios, bytes, sizeof(bytes));
}

bool write_int16(IOStream *ios, int i)
{
    unsigned char bytes[2] = { (i>>8)&255, i&255 };
    return write_data(ios, bytes, sizeof(bytes));
}

bool write_int24(IOStream *ios, int i)
{
    unsigned char bytes[3] = { (i>>16)&255, (i>>8)&255, i&255 };
    return write_data(ios, bytes, sizeof(bytes));
}

bool write_int32(IOStream *ios, int i)
{
    unsigned char bytes[4] = { (i>>24)&255, (i>>16)&255, (i>>8)&255, i&255 };
    return write_data(ios, bytes, sizeof(bytes));
}
