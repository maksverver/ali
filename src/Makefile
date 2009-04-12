# Release flags:
CFLAGS=-fPIC -fvisibility=hidden -Os -DWITH_LZMA
LDFLAGS=-lm

EXECUTABLES=ali alic alidump ali-garglk
COMMON_OBJECTS=dmalloc.o elements.o io.o interpreter.o strings.o \
	ScapegoatTree.o Array.o lzma/lzma.a
LIB_OBJECTS=debug.o $(COMMON_OBJECTS)
ALI_OBJECTS=ali.o $(LIB_OBJECTS)
ALIC_OBJECTS=alic.o syntax.yy.o grammar.tab.o $(LIB_OBJECTS)
ALI_GLK_OBJECTS=debug-glk.o $(COMMON_OBJECTS)
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

debug-glk.o: debug-glk.c
	$(CC) $(CFLAGS) -I../cheapglk -c $<

ali-garglk: $(ALI_GLK_OBJECTS) ali.c
	$(CC) $(CFLAGS) -I../cheapglk -DWITH_GARGLK ali.c -o $@ \
		$(LDFLAGS) $(ALI_GLK_OBJECTS) -Wl,--subsystem,windows -L../gargoyle -lgarglk

ali-glk: $(ALI_GLK_OBJECTS) ali.c
	$(CC) $(CFLAGS) -I../cheapglk -DWITH_GLK ali.c -o $@ \
		$(LDFLAGS) -rdynamic \
		$(ALI_GLK_OBJECTS) ../glkloader/libglkloader.a -ldl

lzma/lzma.a:
	make -C lzma

clean:
	rm -f *.o
	rm -f grammar.tab.c grammar.tab.h syntax.yy.c

distclean: clean
	rm -f $(EXECUTABLES) $(TESTS)
