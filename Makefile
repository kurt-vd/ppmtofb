PROGRAMS=ppmtofb
default: $(PROGRAMS)

CFLAGS	= -Wall -g3 -O0
PREFIX	= /usr/local

-include config.mk
VERSION	= $(shell ./getlocalversion)
CPPFLAGS+= -DVERSION=\"$(VERSION)\"

clean:
	rm -f ppmtofb $(wildcard *.o)

install: $(PROGRAMS)
	install $(PROGRAMS) $(DESTDIR)$(PREFIX)/bin
