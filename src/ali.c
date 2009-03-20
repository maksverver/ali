#include "debug.h"
#include "opcodes.h"
#include "interpreter.h"
#include "strings.h"
#include "Array.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#ifndef WIN32
#include <sys/ioctl.h>
#endif

FILE *fp_transcript = NULL;
FILE *fp_savedvars = NULL;

int get_screen_width()
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

void free_interpreter(Interpreter *i)
{
    AR_destroy(i->output);
    AR_destroy(i->stack);
    free_vars(i->vars);
    free_module(i->mod);
}

/* Filter output string such that:
   - Leading and trailing newline characters are removed.
   - At most two adjacent newline characters occur in the input.
   - Spaces can only follow non-space characters.
*/
void filter_output(char *buf)
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
   characters, so a 80-character line consists of 81 characters in total). */
void line_wrap_output(char *buf, int line_width)
{
    char *last_space = NULL, *last_newline = buf - 1;
    for ( ; *buf != '\0'; ++buf)
    {
        if (*buf == '\n')
        {
            last_space = NULL;
            last_newline = buf;
        }
        else
        if (*buf == ' ')
        {
            last_space = buf;
        }
        else
        if (buf - last_newline > line_width)
        {
            if (last_space != NULL)
            {
                *last_space = '\n';
                last_newline = last_space;
                last_space   = NULL;
            }
        }
    }
}

void process_output(Interpreter *i)
{
    char ch = '\0';
    AR_append(i->output, &ch);
    char *buf = AR_data(i->output);

    filter_output(buf);
    line_wrap_output(buf, get_screen_width());

    if (*buf != '\0')
    {
        fputs(buf, stdout);
        fputs("\n\n", stdout);
        fflush(stdout);

        fputs(buf, fp_transcript);
        fputs("\n\n", fp_transcript);
        fflush(fp_transcript);
    }

    AR_clear(i->output);
}

void quit(Interpreter *I, int code)
{
    if (fp_savedvars != NULL)
        fclose(fp_savedvars);
    if (fp_transcript != NULL)
        fclose(fp_transcript);

    process_output(I);
    free_interpreter(I);
    exit(code);
}

void pause(Interpreter *I)
{
    process_output(I);

    fputs("Press Enter to continue...\n", stdout);
    fflush(stdout);
    char line[1024];
    if (fgets(line, sizeof(line), stdin) == NULL)
    {
        /* error ignored */
    }
}

char *get_time_str()
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    static char buf[32];
    snprintf(buf, sizeof(buf), "%04d%02d%02dT%02d%02d%02d",
        tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
        tm->tm_hour, tm->tm_min, tm->tm_sec);
    return buf;
}

void command_loop(Interpreter *I)
{
    char line[1024];
    for (;;)
    {
        fputs("> ", stdout);
        fflush(stdout);
        char *line_ptr = fgets(line, sizeof(line), stdin);
        if (line_ptr == NULL)
            break;
        fputc('\n', stdout);

        char *eol = strchr(line, '\n');
        if (eol == NULL)
        {
            if (strlen(line) == sizeof(line) - 1)
                warn("Input line was truncated!");
        }
        else
        {
            *eol = '\0';
        }

        normalize(line);
        fprintf(fp_transcript, "%s> %s\n\n", get_time_str(), line);
        process_command(I, line);
        process_output(I);
    }
}

int main(int argc, char *argv[])
{
    Array stack = AR_INIT(sizeof(Value));
    Array output = AR_INIT(sizeof(char));
    Callbacks callbacks = { &quit, &pause };
    Interpreter interpreter;

    const char *path;
    FILE *fp;

    if (argc > 2 || (argc > 1 && argv[1][0] == '-'))
    {
        printf("Usage: ali [<module>]\n");
        return 0;
    }

    memset(&interpreter, 0, sizeof(interpreter));

    /* Attempt to load executable module */
    path = (argc == 2 ? argv[1] :"module.alo");
    fp = fopen(path, "rb");
    if (!fp)
        fatal("Unable to open file \"%s\" for reading.", path);
    interpreter.mod = load_module(fp);
    fclose(fp);
    if (interpreter.mod == NULL)
        fatal("Invalid module file: \"%s\".", path);

    /* Initialize rest of the interpreter */
    interpreter.vars      = alloc_vars(interpreter.mod);
    if (interpreter.vars == NULL)
        fatal("Could not create interpreter state.");
    interpreter.stack     = &stack;
    interpreter.output    = &output;
    interpreter.callbacks = &callbacks;
    interpreter.aux       = NULL;

    /* Open transcript */
    fp_transcript = fopen("transcript.log", "at");

    /* Restart game */
    reinitialize(&interpreter);
    process_output(&interpreter);
    command_loop(&interpreter);
    warn("Unexpected end of input!");
    quit(&interpreter, 1);

    return 0;
}
