# Release flags:
CFLAGS=-Os -DWITH_LZMA
LDFLAGS=-lm

EXECUTABLES=ali alic alidump
LIB_OBJECTS=debug.o elements.o io.o interpreter.o strings.o \
	ScapegoatTree.o Array.o lzma/lzma.a
ALI_OBJECTS=ali.o $(LIB_OBJECTS)
ALIC_OBJECTS=alic.o syntax.yy.o grammar.tab.o $(LIB_OBJECTS)
TESTS=test-ST test-strings

.PHONY: all tests clean distclean

all: $(EXECUTABLES)

tests: $(TESTS)

test-ST: test-ST.o $(LIB_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ test-ST $(LIB_OBJECTS)

test-strings: test-strings.o $(LIB_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ test-strings.o $(LIB_OBJECTS)

syntax.yy.c: syntax.l grammar.tab.h
	$(LEX) -t syntax.l > $@

grammar.tab.h: grammar.y
	$(YACC) -b grammar -d grammar.y

grammar.tab.c: grammar.y
	$(YACC) -b grammar -d grammar.y

ali: $(ALI_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(ALI_OBJECTS)

alic: $(ALIC_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(ALIC_OBJECTS)

alidump: alidump.o
	$(CC) $(LDFLAGS) -o $@ alidump.o

lzma/lzma.a:
	make -C lzma

clean:
	rm -f *.o
	rm -f grammar.tab.c grammar.tab.h syntax.yy.c

distclean: clean
	rm -f $(EXECUTABLES) $(TESTS)
