#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOPCODE 16
static const char *opcodes[NOPCODE] = {
    "NUL", "LLI", "POP", "LDL",
    "STL", "LDG", "STG", "LDI",
    "STI", "JMP", "JNP", "OP1",
    "OP2", "OP3", "CAL", "RET" };

static const char *all_opts = "msfiwgc";   /* all possible options */
static const char *opts = "msfwgc";        /* default options */
static const char *path = "module.alo";    /* default module path */

/* Word table */
static int nword;
const char **words;
static int nnonterm;

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
    if (i & 0x00800000) i |= -1&~0xffffff;
    return (int)i;
}

static int get_int16(const char *data)
{
    const unsigned char *bytes = (const unsigned char*)data;
    unsigned int i = (bytes[0]<<8)|bytes[1];
    if (i & 0x008000) i |= -1&~0xffff;
    return (int)i;
}

static void dump_header(const char *data, size_t size)
{
    int version;

    printf("\n--- header (%d bytes) ---\n", (int)size);
    if (size < 20)
    {
        printf("Header too short! (Should be 20 bytes.)\n");
        return;
    }

    version = get_int16(data);
    printf("File version:                %4d.%d\n", (version >> 8)&255, version&255);
    printf("Number of global variables:  %6d\n", get_int32(data +  4));
    printf("Number of entities:          %6d\n", get_int32(data +  8));
    printf("Number of properties:        %6d\n", get_int32(data + 12));
    printf("Entry point:                 %6d\n", get_int32(data + 16));
}

static int print_strings(const char *data, size_t size)
{
    int n = 0;
    const char *i = data, *j = data + size;
    while (i < j)
    {
        /* Find end of string: */
        const char *k = i;
        while (k < j && *k != '\0') ++k;
        if (k == j)
            printf("String table is not zero-terminated!\n");

        /* Print string */
        printf("%8d: \"", n++);
        while (i < k)
        {
            if (*i == '"')
                printf("\\\"");
            else
            if (*i == '\n')
                printf("\\n");
            else
            if (*i >= 20 && *i < 127)
                printf("%c", *i);
            else
                printf("\\x%02X", (int)*(unsigned char*)i);
            ++i;
        }
        printf("\"\n");
        ++i; /* skip zero character */
    }
    return n;
}

static void dump_string_table(const char *data, size_t size)
{
    printf("\n--- string table (%d bytes) ---\n", (int)size);
    int entries = get_int32(data);
    printf("Number of strings: %d\n", entries);
    data += 4;
    size -= 4;

    int counted = print_strings(data, size);
    if (entries != counted)
        printf("Expected %d entries but counted %d!\n", entries, counted);
}

static void dump_function_table(const char *data, size_t size, int instrs)
{
    int n, entries;

    printf("\n--- function table (%d bytes) ---\n", (int)size);
    if (size < 4)
    {
        printf("Function table too short! (Should be at least 4 bytes.)\n");
        return;
    }
    if (size%4 != 0)
    {
        printf("Invalid function table size (%d); expected multiple of 4!\n",
               (int)size);
        return;
    }

    entries = get_int32(data);
    printf("Number of entries: %d\n\n", entries);
    data += 4, size -= 4;

    if (entries < 0 || entries > (int)((size - 4)/4))
    {
        printf("Invalid number of entries!\n");
        return;
    }

    printf("Function  Arguments Results  \n");
    printf("--------- --------- ---------\n");
    for (n = 0; n < entries; ++n)
    {
        int narg = data[3];
        int nret = data[2];
        data += 4;
        size -= 4;
        printf("%8d: %8d  %8d\n", n, narg, nret);
    }
    printf("--------- --------- --------- -----------\n");

    if (instrs)
        printf("\nInstructions data follows.\n");

    int counted = 0, last_zero = 1;
    for (n = 0; n < (int)(size/4); ++n)
    {
        if (last_zero)
        {
            if (instrs)
                printf("\nFunction %d:\n", counted);
            ++counted;
        }

        int opcode   = data[4*n];
        int argument = get_int24(data + 4*n + 1);

        if (opcode < 0 || opcode >= NOPCODE)
        {
            printf("invalid opcode: %d (argument: %d)\n", opcode, argument);
        }

        if (instrs)
        {
            printf("\t%6d:\t", n);
            if (opcode >= 0 && opcode < NOPCODE)
            {
                printf("%s %8d\n", opcodes[opcode], argument);
            }
        }

        last_zero = (opcode == 0 && argument == 0);
    }

    if (!last_zero != 0)
        printf("Instruction data is not zero-terminated!\n");

    if (counted != entries)
        printf("Function count (%d) does not match specified count (%d)!\n",
               counted, entries);

}

static void dump_word_table(const char *data, size_t size)
{
    printf("\n--- word table (%d bytes) ---\n", (int)size);

    nword = get_int32(data);
    printf("Number of words: %d\n", nword);
    data += 4;
    size -= 4;

    int counted = print_strings(data, size);
    if (nword != counted)
        printf("Expected %d entries but counted %d!\n", nword, counted);

    words = malloc(sizeof(char*)*nword);
    memset(words, 0, sizeof(char*)*nword);

    int n = 0;
    const char *i = data, *j = data + size, *k;
    for (k = i; k != j && n < nword; ++k)
    {
        if (*k == '\0')
        {
            words[n++] = i;
            i = k + 1;
        }
    }
    for ( ; n < nword; ++n)
    {
        words[n] = "?";
        printf("Missing word %d!\n", n);
    }
}

static const char *symbol_str(int index)
{
    static char buf[32];

    if (index < 0 && index >= -nword)
    {
        /* Valid terminal symbol reference */
        snprintf(buf, sizeof(buf), "\"%.29s\"", words[-1 - index]);
        return buf;
    }
    else
    if (index > 0 && index <= nnonterm)
    {
        /* Valid symbol reference */
        --index;
        char *p = &buf[16];
        *--p = '\0';
        do {
            *--p = 'A' + index%26;
            index /= 26;
        } while (index > 0);
        return p;
    }
    else
    {
        /* Invalid reference; just print integer index. */
        snprintf(buf, sizeof(buf), "%d", index);
        return buf;
    }
}

static void dump_grammar_table(const char *data, size_t size)
{
    printf("\n--- grammar table (%d bytes) ---\n", (int)size);
    if (size < 4)
    {
        printf("Grammar table too short! (Should be at least 4 bytes.)\n");
        return;
    }

    nnonterm = get_int32(data);
    int tot_rule = get_int32(data + 4);
    int tot_symref = get_int32(data + 8);
    printf("Number of non-terminal symbols:    %8d\n", nnonterm);
    printf("Total number of rules:             %8d\n", tot_rule);
    printf("Total number of symbol references: %8d\n", tot_symref);
    data += 12;
    size -= 12;

    int n, r, s;
    for (n = 0; n < nnonterm; ++n)
    {
        if (size < 4)
        {
            printf("Grammar table truncated (expected non-terminal)\n");
            break;
        }
        int nrule = get_int32(data);
        data += 4;
        size -= 4;
        for (r = 0; r < nrule; ++r)
        {
            if (size < 4)
            {
                printf("Grammar table truncated (expected rule)\n");
                break;
            }
            int nsymbol = get_int32(data);
            data += 4;
            size -= 4;
            if ((int)(size/4) < nsymbol)
            {
                printf("Grammar table truncated (expected %d symbols)\n",
                       nsymbol);
                data += size;
                size = 0;
                break;
            }

            printf("%8s ->", symbol_str(n + 1));
            for (s = 0; s < nsymbol; ++s)
                printf(" %s", symbol_str(get_int32(data + 4*s)));
            printf("\n");

            data += 4*nsymbol;
            size -= 4*nsymbol;

            tot_symref -= nsymbol;
        }
        tot_rule -= nrule;
    }

    if (tot_rule != 0)
        printf("Rule count does not match declared number of rules!\n");

    if (tot_symref != 0)
        printf("Symbol count does not match declared number of symbols!\n");

    if (size > 0)
        printf("Extra data at end of grammar table!\n");
}

static void dump_command_table(const char *data, size_t size)
{
    printf("\n--- command table (%d bytes) ---\n", (int)size);
    if (size < 4)
    {
        printf("Command table too short! (Should be at least 4 bytes.)\n");
        return;
    }

    int command_sets = get_int32(data);
    data += 4;
    size -= 4;

    printf("Number of command sets: %d\n\n", command_sets);
    if (command_sets < 1)
    {
        printf("Too few command sets! (Should be at least 1)\n");
        return;
    }

    int cs;
    for (cs = 0; cs < command_sets; ++cs)
    {
        if (size < 4)
        {
            printf("Command table truncated! (Command set size expected.)");
            break;
        }

        int num_commands = get_int32(data);
        data += 4;
        size -= 4;

        printf("Command set %d with %d commands follows.\n\n", cs, num_commands);

        printf("Command     Symbol      Guard       Function\n");
        printf("----------- ----------- ----------- -----------\n");
        int n;
        for (n = 0; n < num_commands; ++n)
        {
            if (size < 12)
            {
                printf("Command set truncated!");
                break;
            }

            int symbol   = get_int32(data + 0);
            int guard    = get_int32(data + 4);
            int function = get_int32(data + 8);
            data += 12;
            size -= 12;
            printf("%10d: %10s  %10d  %10d\n", n, symbol_str(symbol), guard, function);
        }
        printf("----------- ----------- ----------- -----------\n");
    }
    if (size > 0)
    {
        printf("Extra data at end of command table!\n");
    }
}

static size_t pad_chunk_size(size_t chunk_size)
{
    return chunk_size + (chunk_size&1);
}

static int start_chunk(const char *id, const char **data, size_t *size,
                        size_t *chunk_size)
{
    if (*size < 8)
    {
        printf("File truncated (expected '%s' chunk).\n", id);
        return 0;
    }

    if (memcmp(*data, id, 4) != 0)
    {
        printf("Unexpected chunk identifier '%.4s' (expected '%s').\n",
               *data, id);
        return 0;
    }

    *chunk_size = get_int32(*data + 4);
    if (pad_chunk_size(*chunk_size) > *size - 8)
    {
        printf("Invalid chunk size %d for '%s' chunk.\n",
               (int)*chunk_size, id);
    }

    *data += 8;
    *size -= 8;

    return 1;
}

static void end_chunk(const char **data, size_t *size, size_t chunk_size)
{
    chunk_size = pad_chunk_size(chunk_size);
    *size -= chunk_size;
    *data += chunk_size;
}

static void dump(const char *opts, const char *data, size_t size)
{
    size_t chunk_size;

    if (size < 12)
    {
        printf("Malformed object signature (file too short).\n");
        return;
    }

    if (memcmp(data, "FORM", 4) != 0)
    {
        printf("Malformed object signature (expected FORM identifier).\n");
        return;
    }

    chunk_size = get_int32(data + 4);

    data += 8;
    size -= 8;

    if (chunk_size != pad_chunk_size(chunk_size) || chunk_size > size)
    {
        printf("Malformed object signature (invalid FORM chunk size: %d)\n",
               (int)chunk_size);
        return;
    }

    if (memcmp(data, "ALI ", 4) != 0)
    {
        printf("Malformed object signature (expected ALI identifier).\n");
        return;
    }
    data += 4;
    size -= 4;

    if (chunk_size < size)
    {
        printf("Warning: extra data at end of file.\n");
        size = chunk_size;
    }

    if (start_chunk("MOD ", &data, &size, &chunk_size))
    {
        if (strchr(opts, 'm') != NULL)
            dump_header(data, chunk_size);
        end_chunk(&data, &size, chunk_size);
    }

    if (start_chunk("STR ", &data, &size, &chunk_size))
    {
        if (strchr(opts, 's') != NULL)
            dump_string_table(data, chunk_size);
        end_chunk(&data, &size, chunk_size);
    }

    if (start_chunk("FUN ", &data, &size, &chunk_size))
    {
        if (strchr(opts, 'f') != NULL || strchr(opts, 'i') != NULL)
            dump_function_table(data, chunk_size, strchr(opts, 'i') != NULL);
        end_chunk(&data, &size, chunk_size);
    }

    if (start_chunk("WRD ", &data, &size, &chunk_size))
    {
        if (strchr(opts, 'w') != NULL)
            dump_word_table(data, chunk_size);
        end_chunk(&data, &size, chunk_size);
    }

    if (start_chunk("GRM ", &data, &size, &chunk_size))
    {
        if (strchr(opts, 'g') != NULL)
            dump_grammar_table(data, chunk_size);
        end_chunk(&data, &size, chunk_size);
    }

    if (start_chunk("CMD ", &data, &size, &chunk_size))
    {
        if (strchr(opts, 'c') != NULL)
            dump_command_table(data, chunk_size);
        end_chunk(&data, &size, chunk_size);
    }

    if (size > 0)
        printf("Warning: extra data at end of ALI FORM chunk.\n");
}

int check_opts()
{
    const char *c;
    for (c = opts; *c; ++c)
        if (strchr(all_opts, *c) == NULL)
            return 0;
    return 1;
}

void usage()
{
    printf("Usage: alidump [-%s] [<module>]\n", all_opts);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    FILE *fp;
    char *data;
    size_t size;

    if (argc > 3)
        usage();

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
        if (argv[1][0] != '-')
            usage();
        if (argv[1][1] != '\0')
            opts = argv[1] + 1;
        path = argv[2];
    }

    if (!check_opts())
        usage();

    /* Open file for reading */
    fp = fopen(path, "rb");
    if (!fp)
    {
        fprintf(stderr, "Could not open \"%s\" for reading!\n", path);
        exit(EXIT_FAILURE);
    }

    /* Determine file size */
    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fprintf(stderr, "Unable to seek in file!\n");
        exit(EXIT_FAILURE);
    }
    size = (size_t)ftell(fp);
    fseek(fp, 0, SEEK_SET);

    /* Allocate memory */
    data = malloc(size);
    if (data == NULL)
    {
        fprintf(stderr, "Unable to allocate memory!\n");
        exit(EXIT_FAILURE);
    }

    /* Copy file to memory */
    if (fread(data, 1, size, fp) != size)
    {
        fprintf(stderr, "Unable to read file into memory!\n");
        exit(EXIT_FAILURE);
    }
    fclose(fp);

    /* Dump selected sections */
    dump(opts, data, size);
    printf("\n");

    /* Free memory */
    free(data);

    return 0;
}
