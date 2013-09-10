default: ppmtofb

CFLAGS	= -Wall -g3 -O0

-include config.mk
VERSION	= $(shell ./getlocalversion)
CPPFLAGS+= -DVERSION=\"$(VERSION)\"

clean:
	rm -f ppmtofb $(wildcard *.o)
