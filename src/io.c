#include "io.h"

int read_int8(FILE *fp)
{
    return (signed char)fgetc(fp);
}

int read_int16(FILE *fp)
{
    return ((signed char)fgetc(fp) << 8) | fgetc(fp);
}

int read_int24(FILE *fp)
{
    return ((signed char)fgetc(fp) << 16) | (fgetc(fp) << 8) | fgetc(fp);
}

int read_int32(FILE *fp)
{
    return ((signed char)fgetc(fp) << 24) | (fgetc(fp) << 16) | (fgetc(fp) << 8) | fgetc(fp);
}

bool write_int8(FILE *fp, int i)
{
    unsigned char bytes[1] = { i&255 };
    return fwrite(bytes, 1, 1, fp);
}

bool write_int16(FILE *fp, int i)
{
    unsigned char bytes[2] = { (i>>8)&255, i&255 };
    return fwrite(bytes, 2, 1, fp);
}

bool write_int24(FILE *fp, int i)
{
    unsigned char bytes[3] = { (i>>16)&255, (i>>8)&255, i&255 };
    return fwrite(bytes, 3, 1, fp);
}

bool write_int32(FILE *fp, int i)
{
    unsigned char bytes[4] = { (i>>24)&255, (i>>16)&255, (i>>8)&255, i&255 };
    return fwrite(bytes, 4, 1, fp);
}
