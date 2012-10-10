default: ppmtofb

VERSION	= $(shell ./getlocalversion)
CFLAGS	= -Wall -g3 -O0
CPPFLAGS= -DVERSION=\"$(VERSION)\"

clean:
	rm -f ppmtofb $(wildcard *.o)
