CFLAGS=-Wall -g
LDFLAGS=

test: syntax.yy.o grammar.tab.o main.o
	gcc $(LDFLAGS) -o $@ $^

syntax.yy.c: syntax.l grammar.tab.h
	$(LEX) -t $< > $@

grammar.tab.h: grammar.y
	$(YACC) -b grammar -d $<

grammar.tab.c: grammar.y
	$(YACC) -b grammar -d $<

clean:
	rm -f *.o grammar.tab.h grammar.tab.c syntax.yy.c

distclean: clean
	rm -f test

.PHONY: clean distclean
