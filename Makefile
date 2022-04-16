# Makefile -- For building the "oligocast" software. This file is used
# implicitly by the "make" command.

## ## ## options for compatibility

# (none at this time)

## ## ## unusual options for testing etc

SANFLAGS=

#SANFLAGS+=-DDEE_TEST # run-time: -dtest:...

#SANFLAGS+=-fsanitize=address -static-libasan
#SANFLAGS+=-fsanitize=pointer-subtract # run-time: export ASAN_OPTIONS=detect_invalid_pointer_pairs=2

#SANFLAGS+=-fsanitize=leak

#SANFLAGS+=-fsanitize=undefined # seems to enable the suboptions below
#SANFLAGS+=-fsanitize=shift
#SANFLAGS+=-fsanitize=shift-exponent
#SANFLAGS+=-fsanitize=shift-base
#SANFLAGS+=-fsanitize=integer-divide-by-zero
#SANFLAGS+=-fsanitize=signed-integer-overflow
#SANFLAGS+=-fsanitize=bounds
#SANFLAGS+=-fsanitize=bounds-strict
#SANFLAGS+=-fsanitize=alignment
#SANFLAGS+=-fsanitize=object-size
#SANFLAGS+=-fsanitize=float-divide-by-zero
#SANFLAGS+=-fsanitize=float-cast-overflow

#SANFLAGS+=-fsanitize=bool
#SANFLAGS+=-fsanitize=enum
#SANFLAGS+=-fsanitize=pointer-overflow
#SANFLAGS+=-fsanitize=builtin

## ## ## compile the code

CFLAGS=$(SANFLAGS) -Wall -g

oligocast: oligocast.c oligocast_compat.c oligocast_config.h oligocast.h
	cc $(CFLAGS) -o oligocast oligocast.c oligocast_compat.c -lm

clean:
	-rm oligocast

## ## ## build the documentation

A2X=a2x

docs: oligocast.manual.html oligocast.1

oligocast.manual.html: oligocast.manual.txt
	$(A2X) -d manpage -f xhtml -v oligocast.manual.txt

oligocast.1: oligocast.manual.txt
	$(A2X) -d manpage -f manpage -v oligocast.manual.txt
