# Release flags:
CFLAGS=-fPIC -fvisibility=hidden -Os -DWITH_LZMA -Wall -Wextra
LDFLAGS=-lm

# Debug:
CFLAGS=-fPIC -O0 -DWITH_LZMA -Wall -Wextra -fmudflap -g
LDFLAGS=-lm -fmudflap -lmudflap

EXECUTABLES=ali alic alidump ali-garglk
COMMON_OBJECTS=dmalloc.o elements.o io.o strings.o interpreter.o parser.o \
	ScapegoatTree.o Array.o lzma/lzma.a
COMMON_LIBS=common.a lzma/lzma.a
ALI_OBJECTS=ali.o debug.o
ALIC_OBJECTS=alic.o syntax.yy.o grammar.tab.o debug.o
ALI_GLK_OBJECTS=debug-glk.o

.PHONY: all clean distclean

all: $(EXECUTABLES)

syntax.yy.c: syntax.l grammar.tab.h
	$(LEX) -t syntax.l > $@

grammar.tab.h: grammar.y
	$(YACC) -b grammar -d grammar.y

grammar.tab.c: grammar.y
	$(YACC) -b grammar -d grammar.y

common.a: $(COMMON_OBJECTS)
	$(AR) crs common.a $(COMMON_OBJECTS)

ali: $(ALI_OBJECTS) $(COMMON_LIBS)
	$(CC) $(LDFLAGS) -o $@ $(ALI_OBJECTS) $(COMMON_LIBS)

alic: $(ALIC_OBJECTS) $(COMMON_LIBS)
	$(CC) $(LDFLAGS) -o $@ $(ALIC_OBJECTS) $(COMMON_LIBS)

alidump: alidump.o
	$(CC) $(LDFLAGS) -o $@ alidump.o

debug-glk.o: debug-glk.c
	$(CC) $(CFLAGS) -I../cheapglk -c debug-glk.c

ali-garglk: $(ALI_GLK_OBJECTS) ali.c $(COMMON_LIBS)
	$(CC) $(CFLAGS) -I../cheapglk -DWITH_GARGLK ali.c -o $@ \
		$(ALI_GLK_OBJECTS) $(COMMON_LIBS) \
		$(LDFLAGS) -L../gargoyle -lgarglk
# TODO: on Windows, add -Wl,--subsystem=windows or -mwindows

ali-glk: $(ALI_GLK_OBJECTS) ali.c $(COMMON_LIBS)
	$(CC) $(CFLAGS) -I../cheapglk -DWITH_GLK ali.c -o $@ \
		$(ALI_GLK_OBJECTS) $(COMMON_LIBS) ../glkloader/libglkloader.a \
		$(LDFLAGS) -rdynamic -ldl

lzma/lzma.a:
	make -C lzma

clean:
	rm -f *.o common.a lzma/lzma.a
	rm -f grammar.tab.c grammar.tab.h syntax.yy.c

distclean: clean
	rm -f $(EXECUTABLES) $(TESTS)
