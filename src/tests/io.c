#include "io.h"
#include <assert.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        printf("Usage: %s <file in> <file out>\n", argv[0]);
        return 1;
    }

/* Read binary data. */
bool read_data(IOStream *ios, void *buf, size_t size);

/* Write binary data. */
bool write_data(IOStream *ios, void *buf, size_t size);

    IOStream in, out;
    if (!ios_open(&in, argv[1], IOM_RDONLY, IOC_AUTO))
    {
        printf("Could not open %s for reading!\n", argv[1]);
        return 1;
    }
    if (!ios_open(&out, argv[2], IOM_WRONLY, IOC_AUTO))
    {
        printf("Could not open %s for writing!\n", argv[2]);
        return 1;
    }

    char block[512];
    while (read_data(&in, &block, 512))
    {
        if (!write_data(&out, &block, 512))
        {
            printf("Could not write byte!\n");
            return 1;
        }
    }

    ios_close(&in);
    ios_close(&out);

    return 0;
}
