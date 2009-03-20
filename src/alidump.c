#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOPCODE 16
static const char *opcodes[NOPCODE] = {
    "NUL", "LLI", "POP", "LDL",
    "STL", "LDG", "STG", "LDI",
    "STI", "JMP", "JNP", "OP1",
    "OP2", "OP3", "CAL", "RET" };

/* This is used for pretty-printing the command table */
static int nverb, nent, nprep;
static char **verbs, **ents, **preps;


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

    printf("\n--- header (%d bytes) ---\n", (int)size);
    if (size < 32)
    {
        printf("Header too short! (Should be 32 bytes.)\n");
        return;
    }

    version = get_int16(data + 4);
    nverb = get_int32(data +  8);
    nprep = get_int32(data + 12);
    nent  = get_int32(data + 16);
    verbs = calloc(nverb, sizeof(char*));
    preps = calloc(nprep, sizeof(char*));
    ents  = calloc(nent,  sizeof(char*));
    printf("File version:                %4d.%d\n", (version >> 8)&255, version&255);
    printf("Number of verbs:             %6d\n", nverb);
    printf("Number of prepositions:      %6d\n", nprep);
    printf("Number of entities:          %6d\n", nent);
    printf("Number of entity properties: %6d\n", get_int32(data + 20));
    printf("Number of global variables:  %6d\n", get_int32(data + 24));
    printf("Entry point:                 %6d\n", get_int32(data + 28));
}

static void dump_fragment_table(const char *data, size_t size)
{
    int entries, n;

    printf("\n--- fragment table (%d bytes) ---\n", (int)size);
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
        int flags  = (*(data + 8 + 8*n) & 0xf0) >> 4;
        int type   = (*(data + 8 + 8*n) & 0x0f);
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
        printf(" => %s %d", type == 0 ? "verb" : type == 1 ? "preposition" :
                            type == 2 ? "entity" : "<invalid type>", id);
        if (flags&1) printf(" (canonical)");
        printf("\n");

        if ((flags&1) && type == 0 && verbs != NULL
            && id >= 0 && id < nverb && verbs[id] == NULL)
            verbs[id] = strdup(data + offset);

        if ((flags&1) && type == 1 && preps != NULL
             && id >= 0 && id < nprep && preps[id] == NULL)
            preps[id] = strdup(data + offset);

        if ((flags&1) && type == 2 && ents != NULL
             && id >= 0 && id < nent && ents[id] == NULL)
            ents[id] = strdup(data + offset);
    }
}

static void dump_string_table(const char *data, size_t size)
{
    int n, entries;

    printf("\n--- string table (%d bytes) ---\n", (int)size);
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

static void dump_function_table(const char *data, size_t size, int instrs)
{
    int n, entries;

    printf("\n--- function table (%d bytes) ---\n", (int)size);
    if (size < 8)
    {
        printf("Function table too short! (Should be at least 8 bytes.)\n");
        return;
    }

    entries = get_int32(data + 4);
    printf("Number of entries: %d\n\n", entries);
    data += 8, size -= 8;

    if (entries < 0 || entries > (size - 8)/8)
    {
        printf("Invalid number of entries!\n");
        return;
    }

    printf("Function  Arguments Results   Offset    Size      1st Instr.\n");
    printf("--------- --------- --------- --------- --------- ---------\n");
    for (n = 0; n < entries; ++n)
    {
        int narg   = *(data + 8*n + 3);
        int nret   = *(data + 8*n + 2);
        int offset = get_int32(data + 8*n + 4);
        int next_offset = (n + 1 < entries) ? get_int32(data + 8*(n + 1) + 4)
                                            : size + 8;

        printf("%8d: %8d  %8d  %8d  %8d  %8d \n",
               n, narg, nret, offset, next_offset - offset,
               (offset - (8 + 8*entries))/4);

        if (offset%4 != 0 || offset < 8 + 8*entries || offset > size)
            printf("Code offset is invalid!\n");
    }
    printf("--------- --------- --------- --------- --------- -----------\n");

    data += 8*entries, size -= 8*entries;

    if (instrs)
    {
        printf("\nInstructions:\n");
        for (n = 0; n < size/4; ++n)
        {
            int opcode, argument;
            opcode   = data[4*n];
            argument = (data[4*n + 1] << 16) |
                       (data[4*n + 2] <<  8) |
                       (data[4*n + 3] <<  0);

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
}

static const char *get_verb(int id)
{
    if (id < 0 || id >= nverb) return "<?>";
    return verbs[id] == NULL ? "<NULL>" : verbs[id];
}

static const char *get_prep(int id)
{
    if (id < 0 || id >= nprep) return "<?>";
    return preps[id] == NULL ? "<NULL>" : preps[id];
}

static const char *get_ent(int id)
{
    if (id < 0 || id >= nent) return "<?>";
    return ents[id] == NULL ? "<NULL>" : ents[id];
}

static void dump_command_table(const char *data, size_t size)
{
    int n, entries;

    printf("\n--- command table (%d bytes) ---\n", (int)size);
    if (size < 8)
    {
        printf("Command table too short! (Should be at least 8 bytes.)\n");
        return;
    }

    entries = get_int32(data + 4);
    printf("Number of entries: %d\n\n", entries);
    data += 8, size -= 8;

    if (entries < 0)
    {
        printf("Invalid number of entries!\n");
        return;
    }

    printf("Command  Form    Verb   Object  Prepos. Obj. 2  Guard   Function\n");
    printf("------- ------- ------- ------- ------- ------- ------- -------\n");
    for (n = 0; n < entries; ++n)
    {
        int form, args, verb, obj1, prep, obj2, guard, func;

        if (size < 4)
        {
            printf("Command table entry truncated!\n");
            return;
        }
        form = get_int16(data);
        args = get_int16(data + 2);
        if (size < 4 + 4*args + 8)
        {
            printf("Command table entry truncated!\n");
            return;
        }
        switch (form)
        {
        case 0:
            if (args != 1)
            {
                printf("Invalid number of arguments: %d (expected 1).\n", args);
                return;
            }
            verb = get_int32(data + 4);
            obj1 = prep = obj2 = -1;
            break;

        case 1:
            if (args != 2)
            {
                printf("Invalid number of arguments: %d (expected 2).\n", args);
                return;
            }
            verb = get_int32(data + 4);
            obj1 = get_int32(data + 8);
            prep = obj2 = -1;
            break;

        case 2:
            if (args != 4)
            {
                printf("Invalid number of arguments: %d (expected 4).\n", args);
                return;
            }
            verb = get_int32(data +  4);
            obj1 = get_int32(data +  8);
            prep = get_int32(data + 12);
            obj2 = get_int32(data + 16);
            break;

        default:
            printf("Unrecognized command (form %d, %d argumens)", form, args);
        }
        guard = get_int32(data + 4 + 4*args);
        func  = get_int32(data + 4 + 4*args + 4);
        data += 4 + 4*args + 8;
        size -= 4 + 4*args + 8;

        printf("%6d: %6d  %6d", n, form, verb);
        if (obj1 == -1)
            printf("       -");
        else
            printf("  %6d", obj1);
        if (prep == -1)
            printf("       -");
        else
            printf("  %6d", prep);
        if (obj2 == -1)
            printf("       -");
        else
            printf("  %6d", obj2);
        if (guard == -1)
            printf("       -");
        else
            printf("  %6d", obj2);
        printf("  %6d\n", func);


        printf("        ");
        if (verb != -1) printf(" %s", get_verb(verb));
        if (obj1 != -1) printf(" %s", get_ent(obj1));
        if (prep != -1) printf(" %s", get_prep(prep));
        if (obj2 != -1) printf(" %s", get_ent(obj2));
        printf("\n");
    }
    printf("------- ------- ------- ------- ------- ------- ------- -------\n");
}

static void dump(const char *opts, const char *data, size_t size)
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
    if (strchr(opts, 'h') != NULL)
        dump_header(data, section_size);
    data += section_size, size -= section_size;

    if (size < 4 || (section_size = (size_t)get_int32(data)) > size || size%4 != 0)
    {
        printf("Invalid fragment table size.\n");
        return;
    }
    if (strchr(opts, 'f') != NULL)
        dump_fragment_table(data, section_size);
    data += section_size, size -= section_size;

    if (size < 4 || (section_size = (size_t)get_int32(data)) > size || size%4 != 0)
    {
        printf("Invalid string table size.\n");
        return;
    }
    if (strchr(opts, 's') != NULL)
        dump_string_table(data, section_size);
    data += section_size, size -= section_size;

    if (size < 4 || (section_size = (size_t)get_int32(data)) > size || size%4 != 0)
    {
        printf("Invalid function table size.\n");
        return;
    }
    if (strchr(opts, 'u') != NULL || strchr(opts, 'i') != NULL)
        dump_function_table(data, section_size, strchr(opts, 'i') != NULL);
    data += section_size, size -= section_size;

    if (size < 4 || (section_size = (size_t)get_int32(data)) > size || size%4 != 0)
    {
        printf("Invalid command table size.\n");
        return;
    }
    if (strchr(opts, 'c') != NULL)
        dump_command_table(data, section_size);
    data += section_size, size -= section_size;
}

int main(int argc, char *argv[])
{
    const char *opts = "hfsuc", *path = "module.alo";
    FILE *fp;
    char *data;
    size_t size;
    int n;

    if (argc > 3 || (argc > 2 && argv[1][0] != '-'))
    {
        printf("Usage: alidump -[hfsuic] [<module>]\n");
        return 0;
    }
    if (argc == 2)
    {
        if (argv[1][0] == '-')
            opts = argv[1] + 1;
        else
            path = argv[1];
    }
    else
    if (argc == 3)
    {
        if (argv[1][1] != '\0') opts = argv[1] + 1;
        path = argv[2];
    }

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
    size = (size_t)ftell(fp);
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

    /* Dump selected sections */
    dump(opts, data, size);
    printf("\n");

    /* Free memory */
    free(data);
    for (n = 0; n < nverb; ++n)
        free(verbs[n]);
    free(verbs);
    for (n = 0; n < nprep; ++n)
        free(preps[n]);
    free(preps);
    for (n = 0; n < nent; ++n)
        free(ents[n]);
    free(ents);

    return 0;
}
