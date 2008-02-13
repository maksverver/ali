#ifndef STRINGS_H_INCLUDED
#define STRINGS_H_INCLUDED

const char *internalize(const char *str);

/* Normalizes a command string. Note that the argument string is modified! */
char *normalize(char *str);

#endif /* ndef STRINGS_H_INCLUDED */
