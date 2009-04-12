#include "debug.h"
#include "io.h"
#include "opcodes.h"
#include "interpreter.h"
#include "strings.h"
#include "Array.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef _MSC_VER
#define EXPORT __declspec(dllexport)
#else /* gcc */
#define EXPORT __attribute__ ((visibility("default")))
#endif

#ifdef WITH_GARGLK
#define WITH_GLK
#endif

#ifdef WITH_GLK

#include <glk.h>
#include <glkstart.h>

#else /* ndef WITH_GLK */

#ifdef WIN32  /* Windows */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else  /* POSIX */
#include <sys/ioctl.h>
#endif

#ifdef WIN32
static HANDLE hStdOut;
#endif

#endif /* ndef WITH_GLK */


static void ali_quit(Interpreter *I, int code);
static void ali_pause(Interpreter *I);


/* Global variables: */
static const char *module_path = "module.alo";
static FILE *fp_transcript = NULL;      /* transcript file handle */
static FILE *fp_savedgame = NULL;       /* saved game file handle */

/* Interpreter state: */
static Interpreter interpreter;
static Interpreter * const I = &interpreter;
static Array stack = AR_INIT(sizeof(Value));
static Array output = AR_INIT(sizeof(char));
static Callbacks callbacks = { &ali_quit, &ali_pause };

#ifdef WITH_GLK
static winid_t mainwin;
#endif


#ifndef WITH_GLK
static int get_screen_width()
{
#ifdef WIN32
    /* Return 79 on windows, because lines of length 80 already wrap! */
    return 79;
#else
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0)
        return ws.ws_col;
    else
        return 80;
#endif
}
#endif /* ndef WITH_GLK */

static void free_interpreter(Interpreter *I)
{
    AR_destroy(I->output);
    AR_destroy(I->stack);
    free_vars(I->vars);
    free_module(I->mod);
}

/* Filter output string such that:
   - Leading and trailing newline characters are removed.
   - At most two adjacent newline characters occur in the input.
   - Spaces can only follow non-space characters.
*/
static void filter_output(char *buf)
{
    int num_newlines = 2, num_spaces = 2;
    char *in, *out;
    for (in = out = buf; *in != '\0'; ++in)
    {
        switch (*in)
        {
        case '\n':
            if (num_newlines < 2)
            {
                *out++ = '\n';
                num_newlines++;
                num_spaces++;
            }
            break;

        case '\t':  /* ignore tabs for now */
        case ' ':
            if (num_spaces == 0)
            {
                *out++ = ' ';
                num_spaces++;
            }
            break;

        default:
            *out++ = *in;
            num_newlines = num_spaces = 0;
            break;
        }
    }

    if (out > buf)
        out -= num_newlines;
    *out = '\0';
}

/* Ensures lines consist of at most line_width characters (not counting newline
   characters, so a 80-character line consists of 81 characters in total).
   Formatting characters are ignored. */
static void line_wrap_output(char *buf, int line_width)
{
    char *last_space = NULL, *last_newline = buf - 1;
    int num_ignored = 0;
    for ( ; *buf != '\0'; ++buf)
    {
        switch (*buf)
        {
        case '\n':
            last_space = NULL;
            last_newline = buf;
            num_ignored = 0;
            break;

        case ' ':
            last_space = buf;
            break;

        case '*':
        case '~':
            ++num_ignored;
            break;

        default:
            if (buf - last_newline - num_ignored > line_width)
            {
                if (last_space != NULL)
                {
                    *last_space = '\n';
                    last_newline = last_space;
                    last_space   = NULL;
                    num_ignored  = 0;
                }
            }
        }
    }
}

/* Convert UTF-8 sequence to corresponding Latin-1 characters; unsupported
   characters are silently removed. */
static void utf8_to_latin1(char *in)
{
    char *out = in;
    while (*in)
    {
        if ((*in & 0x80) == 0)
        {
            *out++ = *in++; // copy single-byte character
        }
        else
        {
            if ((*in & 0xe0) == 0xc0) // decode two-byte character
            {
                int ch = ((in[0] & 0x1f) << 6) | (in[1] & 0x3f);
                in += 2;
                if (ch < 256) *out++ = ch;
            }
            /* in principle we should decode three and four byte encodings too,
               but in practice these are rarely used for latin-1 characters. */
        }
    }
    *out = '\0';
}

static void set_prompt()
{
#ifdef WITH_GLK
    glk_set_style(style_Emphasized);
#else
#ifdef WIN32
    SetConsoleTextAttribute(hStdOut, FOREGROUND_RED|FOREGROUND_GREEN);
#else
    fputs("\033[33m", stdout);  /* ANSI code for dark yellow */
#endif
#endif /* ndef WITH_GLK */
}

static void set_bold()
{
#ifdef WITH_GLK
    glk_set_style(style_Emphasized);
#else
#ifdef WIN32
    SetConsoleTextAttribute(hStdOut, FOREGROUND_RED | FOREGROUND_GREEN |
                                     FOREGROUND_BLUE | FOREGROUND_INTENSITY);
#else
    fputs("\033[1m", stdout);  /* ANSI code for high intensity */
#endif
#endif /* ndef WITH_GLK */
}

static void set_normal()
{
#ifdef WITH_GLK
    glk_set_style(style_Normal);
#else
#ifdef WIN32
    SetConsoleTextAttribute(hStdOut,
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
#else
    fputs("\033[0m", stdout);  /* ANSI code for normal intensity */
#endif
#endif /* ndef WITH_GLK */
}

static void write_ch(int ch)
{
#ifdef WITH_GLK
    glk_put_char(ch);
#else
    fputc(ch, stdout);
#endif
}

static char *read_line()
{
    static char line[1024];
#ifdef WITH_GLK
    glk_request_line_event(mainwin, line, sizeof(line) - 1, 0);
    for (;;)
    {
        event_t ev;
        glk_select(&ev);
        if (ev.type == evtype_LineInput && ev.win == mainwin)
        {
            line[ev.val1] = '\0';
            return line;
        }
    }
#else
    fflush(stdout);
    if (fgets(line, sizeof(line), stdin) == NULL)
        return NULL;
    return line;
#endif /* ndef WITH_GLK */
}

static void write_str(const char *s)
{
#ifdef WITH_GLK
    glk_put_string((char*)s);
#else
    fputs(s, stdout);
#endif
}

static void write_fmt(const char *format, ...)
{
    char buf[1024];
    va_list ap;

    va_start(ap, format);
    vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    return write_str(buf);
}

static const char *get_string_var(Interpreter *I, int var, const char *def)
{
    if (var < 0 || var >= I->vars->nval)
        return def;
    int str = (int)I->vars->vals[var];
    if (str < 0 || str >= I->mod->nstring)
        return def;
    return I->mod->strings[str];
}

static void process_output(Interpreter *I)
{
#ifdef WITH_GLK
    /* This doesn't really seem to work as advertised 
       (at least not with Gargoyle) */
/*
    const char *header_str = get_string_var(I, var_title, NULL);
    if (header_str != NULL)
    {
        glk_set_style(style_Header);
        glk_put_string((char*)header_str);
        glk_put_char('\n');
    }
    const char *subheader_str = get_string_var(I, var_subtitle, NULL);
    if (subheader_str != NULL)
    {
        glk_set_style(style_Subheader);
        glk_put_string((char*)subheader_str);
        glk_put_char('\n');
    }
*/
#endif

    char ch = '\0';
    AR_append(I->output, &ch);
    char *p, *buf = AR_data(I->output);

    filter_output(buf);
#ifdef WITH_GLK
    utf8_to_latin1(buf);
#else
    line_wrap_output(buf, get_screen_width());
#endif /* ndef WITH_GLK */

    if (*buf != '\0')
    {
        set_normal();
        bool bold = false;
        for (p = buf; *p != '\0'; ++p)
        {
            switch (*p)
            {
            case '*':
                bold = !bold;
                if (bold)
                    set_bold();
                else
                    set_normal();
                break;

            case '~':
                write_ch('"');
                break;

            default:
                write_ch(*p);
            }
        }
        if (bold)
            set_normal();
        write_ch('\n');
        write_ch('\n');

        if (fp_transcript != NULL)
        {
            fputs(buf, fp_transcript);
            fputs("\n\n", fp_transcript);
            fflush(fp_transcript);
        }
    }

    AR_clear(I->output);
}

static void do_exit(int code)
{
#ifdef WITH_GLK
    (void)code;  /* ignored */
    glk_exit();
#else
    exit(code);
#endif
}

static void ali_quit(Interpreter *I, int code)
{
    if (fp_savedgame != NULL)
        fclose(fp_savedgame);
    if (fp_transcript != NULL)
        fclose(fp_transcript);

    process_output(I);
    free_interpreter(I);
    do_exit(code);
}

static void ali_pause(Interpreter *I)
{
    process_output(I);
    write_str("Press Enter to continue...\n");
    read_line();
}

static char *get_time_str()
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    static char buf[32];
    snprintf(buf, sizeof(buf), "%04d%02d%02dT%02d%02d%02d",
        tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
        tm->tm_hour, tm->tm_min, tm->tm_sec);
    return buf;
}

static void load_game(Interpreter *I)
{
    fseek(fp_savedgame, 0, SEEK_SET);
    if (fread(I->vars->vals, sizeof(Value), I->vars->nval, fp_savedgame)
        != (size_t)I->vars->nval)
    {
        fatal("Could not load game data!");
    }
}

static void save_game(Interpreter *I)
{
    fseek(fp_savedgame, 0, SEEK_SET);
    if (fwrite(I->vars->vals, sizeof(Value), I->vars->nval, fp_savedgame)
        != (size_t)I->vars->nval)
    {
        fatal("Could not save game data!");
    }
    fflush(fp_savedgame);
}

static void command_loop(Interpreter *I)
{
    for (;;)
    {
        set_prompt();
        write_str("> ");
        char *line = read_line();
        set_normal();
        write_str("\n");
        if (line == NULL)
            break;
        normalize(line);
        if (fp_transcript != NULL)
            fprintf(fp_transcript, "%s> %s\n\n", get_time_str(), line);
        process_command(I, line);
        process_output(I);
        save_game(I);
    }
}

static void select_game(Interpreter *I)
{
    char filename[128];
    int n, c;
    for (n = 1; ; n++)
    {
        struct stat st;
        snprintf(filename, sizeof(filename), "savedgame-%d.bin", n);
        if (stat(filename, &st) == 0 && S_ISREG(st.st_mode))
        {
            if (n == 1)
            {
                set_bold();
                write_str("Welcome back!\n");
                set_normal();
                write_str("\nWould you like to:\n");
            }
            struct tm *tm = localtime(&st.st_ctime);
            write_fmt( "%3d) Resume saved game %d, "
                       "last played on %04d/%02d/%02d %02d:%02d\n",
                       n, n, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                       tm->tm_hour, tm->tm_min );
        }
        else
        {
            if (n > 1)
            {
                write_fmt("%3d) Start a new game\n", n);
                write_str("  0) Quit\n");
            }
            break;
        }
    }

    if (n == 1)
    {
        /* Nothing to choose; start a new game. */
        c = 1;
    }
    else
    {
        for (;;)
        {
            set_prompt();
            write_str("\n> ");

            char *line = read_line();
            if (line == NULL)
                fatal("Failed to read input.");
            set_normal();
            if (sscanf(line, "%d", &c) != 1)
            {
                write_str("\nResponse not understood.\n");
                continue;
            }
            if (c < 0 || c > n)
            {
                write_fmt("\nPlease select an option between 0 and %d.\n", n);
                continue;
            }
            break;
        }
    }

    if (c == 0)
        do_exit(0);

    /* Open saved game */
    snprintf(filename, sizeof(filename), "savedgame-%d.bin", c);
    fp_savedgame = fopen(filename, (c < n) ? "r+b" : "wb");
    if (fp_savedgame == NULL)
        fatal("Could not open %s!", filename);

    /* Open transcript */
    snprintf(filename, sizeof(filename), "transcript-%d.txt", c);
    fp_transcript = fopen(filename, "at");
    if (fp_transcript == NULL)
        error("Could not open %s!", filename);

    if (c < n)
    {
        write_fmt("\nResuming game %d.\n\n", c);
        load_game(I);
    }
    else
    {
        write_ch('\n');
        reinitialize(I);
        save_game(I);
        process_output(I);
    }
}

static void do_main()
{
#ifdef WITH_GLK
    mainwin = glk_window_open(0, 0, 0, wintype_TextBuffer, 0);
    if (!mainwin)
        return;
    glk_set_window(mainwin);
#endif

    IOStream ios;

    /* Attempt to load executable module */
    if(!ios_open(&ios, module_path, IOM_RDONLY, IOC_AUTO))
        fatal("Unable to open file \"%s\" for reading.", module_path);
    interpreter.mod = load_module(&ios);
    ios_close(&ios);
    if (interpreter.mod == NULL)
        fatal("Invalid module file: \"%s\".", module_path);

    /* Initialize rest of the interpreter */
    interpreter.vars      = alloc_vars(interpreter.mod);
    if (interpreter.vars == NULL)
        fatal("Could not create interpreter state.");
    interpreter.stack     = &stack;
    interpreter.output    = &output;
    interpreter.callbacks = &callbacks;
    interpreter.aux       = NULL;

    /* Load game */
    select_game(&interpreter);
    command_loop(&interpreter);
    warn("Unexpected end of input!");
    ali_quit(&interpreter, 1);
}

#ifdef WITH_GLK

EXPORT void glk_main(void)
{
    do_main();
}

#else

int main(int argc, char *argv[])
{
#ifdef WIN32
    hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
#endif

    if (argc > 2 || (argc > 1 && argv[1][0] == '-'))
    {
        printf("Usage: ali [<module>]\n");
        return 0;
    }

    if (argc > 1)
        module_path = argv[1];

    do_main();

    return 0;
}
#endif /* ndef WITH_GLK */

#ifdef WITH_GARGLK
int main(int argc, char *argv[])
{
    glkunix_startup_t startdata;
    startdata.argc = argc;
    startdata.argv = malloc(argc * sizeof(char*));
    memcpy(startdata.argv, argv, argc * sizeof(char*));

    gli_startup(argc, argv);

    if (!glkunix_startup_code(&startdata))
            glk_exit();

    glk_main();
    glk_exit();

    return 0;
}
#endif
