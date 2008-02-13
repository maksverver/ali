#ifndef ELEMENTS_H_INCLUDED
#define ELEMENTS_H_INCLUDED

/* Defines functions that allow containers to work manipulate elements. */
typedef int (*EA_cmp) (const void *, const void *);
typedef void *(*EA_dup) (const void *);
typedef void (*EA_free) (void *);

/* Dummy dup function that returns its argument unmodified. */
void *EA_no_dup(const void *arg);

/* Dummy free function that does nothing. */
void EA_no_free(void *arg);

#endif /* ndef ELEMENTS_H_INCLUDED */

