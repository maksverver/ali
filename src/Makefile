# Debug flags:
CFLAGS=-Wall -g -Os -fmudflap
LDFLAGS=-lm -fmudflap -lmudflap

# Release flags:
#CFLAGS=-Os
#LDFLAGS=-lm

EXECUTABLES=ali alic alidump
LIB_OBJECTS=debug.o elements.o io.o interpreter.o strings.o \
	ScapegoatTree.o Array.o
ALI_OBJECTS=ali.o $(LIB_OBJECTS)
ALIC_OBJECTS=alic.o syntax.yy.o grammar.tab.o $(LIB_OBJECTS)
TESTS=test-ST test-strings

.PHONY: all tests clean distclean

all: $(EXECUTABLES)

tests: $(TESTS)

test-ST: test-ST.o $(LIB_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

test-strings: test-strings.o $(LIB_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

syntax.yy.c: syntax.l grammar.tab.h
	$(LEX) -t $< > $@

grammar.tab.h: grammar.y
	$(YACC) -b grammar -d $<

grammar.tab.c: grammar.y
	$(YACC) -b grammar -d $<

ali: $(ALI_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

alic: $(ALIC_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

alidump: alidump.o
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	rm -f *.o
	rm -f grammar.tab.c grammar.tab.h syntax.yy.c

distclean: clean
	rm -f $(EXECUTABLES) $(TESTS)

