#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOPCODE 16
static const char *opcodes[NOPCODE] = {
    "NUL", "LLI", "POP", "LDL",
    "STL", "LDG", "STG", "LDI",
    "STI", "JMP", "JNP", "OP1",
    "OP2", "OP3", "CAL", "RET" };

static int get_int32(const char *data)
{
    const unsigned char *bytes = (const unsigned char*)data;
    unsigned int i = (bytes[0]<<24)|(bytes[1]<<16)|(bytes[2]<<8)|bytes[3];
    return (int)i;
}

static int get_int24(const char *data)
{
    const unsigned char *bytes = (const unsigned char*)data;
    unsigned int i = (bytes[0]<<16)|(bytes[1]<<8)|bytes[2];
    return (int)i;
}

static int get_int16(const char *data)
{
    const unsigned char *bytes = (const unsigned char*)data;
    unsigned int i = (bytes[0]<<8)|bytes[1];
    return (int)i;
}

static void dump_header(const char *data, size_t size)
{
    int version;

    printf("\n--- header (%d bytes) ---\n", size);
    if (size < 32)
    {
        printf("Header too short! (Should be 32 bytes.)\n");
        return;
    }

    version = get_int16(data + 4);
    printf("File version:                %4d.%d\n", (version >> 8)&255, version&255);
    printf("Number of verbs:             %6d\n", get_int32(data +  8));
    printf("Number of prepositions:      %6d\n", get_int32(data + 12));
    printf("Number of entities:          %6d\n", get_int32(data + 16));
    printf("Number of entity properties: %6d\n", get_int32(data + 20));
    printf("Number of global variables:  %6d\n", get_int32(data + 24));
    printf("Entry point:                 %6d\n", get_int32(data + 28));
}

static void dump_fragment_table(const char *data, size_t size)
{
    int entries, n;

    printf("\n--- fragment table (%d bytes) ---\n", size);
    if (size < 8)
    {
        printf("Fragment table too short! (Should be at least 8 bytes.)\n");
        return;
    }

    entries = get_int32(data + 4);
    printf("Number of entries: %d\n", entries);

    if (entries < 0 || entries > (size - 8)/8)
    {
        printf("Invalid number of entries!\n");
        return;
    }

    for (n = 0; n < entries; ++n)
    {
        int type   = *(data + 8 + 8*n);
        int id     = get_int24(data + 8 + 8*n + 1);
        int offset = get_int32(data + 8 + 8*n + 4);

        printf("\t%6d: ", n);
        if (offset < 8 + 8*entries || offset > size)
            printf("<invalid offset: %d>", offset);
        else
        {
            int length = 0;
            while (offset + length < size && data[offset + length] != '\0')
                ++length;
            if (offset + length == size)
                printf("<missing terminating nul-character>");
            else
                printf("\"%s\" (offset: %d; length: %d)", data + offset, offset, length);
        }
        printf(" => %s %d\n", type == 0 ? "verb" : type == 1 ? "proposition" :
                              type == 2 ? "entity" : "<invalid type>", id);
    }
}

static void dump_string_table(const char *data, size_t size)
{
    int n, entries;

    printf("\n--- string table (%d bytes) ---\n", size);
    if (size < 8)
    {
        printf("String table too short! (Should be at least 8 bytes.)\n");
        return;
    }

    entries = get_int32(data + 4);
    printf("Number of entries: %d\n", entries);

    if (entries < 0 || entries > (size - 8)/4)
    {
        printf("Invalid number of entries!\n");
        return;
    }

    for (n = 0; n < entries; ++n)
    {
        int offset = get_int32(data + 8 + 4*n);

        printf("\t%6d: ", n);
        if (offset < 8 + 4*entries || offset > size)
            printf("<invalid offset: %d>\n", offset);
        else
        {
            int length = 0;
            while (offset + length < size && data[offset + length] != '\0')
                ++length;
            if (offset + length == size)
                printf("<missing terminating nul-character>\n");
            else
                printf("\"%s\" (offset: %d; length: %d) \n", data + offset, offset, length);
        }
    }
}

static void dump_function_table(const char *data, size_t size)
{
    int n, entries;

    printf("\n--- function table (%d bytes) ---\n", size);
    if (size < 8)
    {
        printf("Function table too short! (Should be at least 8 bytes.)\n");
        return;
    }

    entries = get_int32(data + 4);
    printf("Number of entries: %d\n", entries);
    data += 8, size -= 8;

    if (entries < 0 || entries > (size - 8)/8)
    {
        printf("Invalid number of entries!\n");
        return;
    }
    for (n = 0; n < entries; ++n)
    {
        int args   = *(data + 8*n + 3);
        int offset = get_int32(data + 8*n + 4);

        printf("\nFunction %d:\n"
               "\tArguments:         %6d\n"
               "\tCode offset:       %6d\n"
               "\tStart instruction: %6d\n",
               n, args, offset, (offset - (8 + 8*entries))/4);

        if (offset%4 != 0 || offset < 8 + 8*entries || offset > size)
            printf("Code offset is invalid!\n");
    }
    data += 8*entries, size -= 8*entries;

    printf("\nInstructions:\n");
    for (n = 0; n < size/4; ++n)
    {
        int opcode, argument;
        opcode   = data[4*n]&255;
        argument = (data[4*n + 1] << 16)| ((data[4*n + 2]&255) << 8) | (data[4*n + 3]&255);

        printf("\t%6d:\t", n);
        if (opcode == 0 && argument == 0)
            printf("---\n");
        else
        if (opcode < 0 || opcode >= NOPCODE)
            printf("invalid opcode: %d (argument: %d)\n", opcode, argument);
        else
            printf("%s %8d\n", opcodes[opcode], argument);
    }
}

static void dump_command_table(const char *data, size_t size)
{
    printf("\n--- command table (%d bytes) ---\n", size);
    printf("TODO\n");
}

static void dump(const char *data, size_t size)
{
    size_t section_size;

    if (size < 4 || memcmp(data, "alio", 4) != 0)
    {
        printf("Missing or malformed object signature.\n");
        return;
    }
    data += 4, size -= 4;

    if (size < 4 || (section_size = (size_t)get_int32(data)) > size || size%4 != 0)
    {
        printf("Invalid header size.\n");
        return;
    }
    dump_header(data, section_size);
    data += section_size, size -= section_size;

    if (size < 4 || (section_size = (size_t)get_int32(data)) > size || size%4 != 0)
    {
        printf("Invalid fragment table size.\n");
        return;
    }
    dump_fragment_table(data, section_size);
    data += section_size, size -= section_size;

    if (size < 4 || (section_size = (size_t)get_int32(data)) > size || size%4 != 0)
    {
        printf("Invalid string table size.\n");
        return;
    }
    dump_string_table(data, section_size);
    data += section_size, size -= section_size;

    if (size < 4 || (section_size = (size_t)get_int32(data)) > size || size%4 != 0)
    {
        printf("Invalid function table size.\n");
        return;
    }
    dump_function_table(data, section_size);
    data += section_size, size -= section_size;

    if (size < 4 || (section_size = (size_t)get_int32(data)) > size || size%4 != 0)
    {
        printf("Invalid command table size.\n");
        return;
    }
    dump_command_table(data, section_size);
    data += section_size, size -= section_size;
}

int main(int argc, char *argv[])
{
    const char *path;
    FILE *fp;
    char *data;
    size_t size;

    if (argc > 2)
    {
        printf("Usage: alidump [<module>]\n");
        return 0;
    }

    if (argc == 2)
        path = argv[1];
    else
        path = "module.alo";

    /* Open file for reading */
    fp = fopen(path, "rb");
    if (!fp)
    {
        fprintf(stderr, "Could not open \"%s\" for reading!\n", path);
        exit(1);
    }

    /* Determine file size */
    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fprintf(stderr, "Unable to seek in file!\n");
        exit(1);
    }
    size = (size_t)ftello(fp);
    fseek(fp, 0, SEEK_SET);

    /* Allocate memory */
    data = malloc(size);
    if (data == NULL)
    {
        fprintf(stderr, "Unable to allocate memory!\n");
        exit(1);
    }

    /* Copy file to memory */
    if (fread(data, 1, size, fp) != size)
    {
        fprintf(stderr, "Unable to read file into memory!\n");
        exit(1);
    }
    fclose(fp);

    dump(data, size);

    return 0;
}
