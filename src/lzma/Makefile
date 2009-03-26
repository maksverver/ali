OBJECTS=LzmaDec.o
CFLAGS=-Os

lzma.a: $(OBJECTS)
	ar crs $@ $(OBJECTS)

clean:
	rm -f $(OBJECTS)

distclean: clean
	rm -f lzma.a
