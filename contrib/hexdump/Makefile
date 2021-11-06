# Makefile for hexdump

CFLAGS	= -O
LDFLAGS	=

OBJ	= hexdump.o

all:	hexdump

hexdump: $(OBJ)
	cc $(LDFLAGS) -o $@ $(OBJ)
	install hexdump

install:	/usr/local/bin/hexdump

/usr/local/bin/hexdump:	hexdump
	install -cs -o bin hexdump $@

$(OBJ):	hexdump.c

clean:
	rm -f hexdump *.o *.bak core
