oligocast: oligocast.c oligocast_compat.c oligocast_config.h oligocast.h
	cc -Wall -g -o oligocast oligocast.c oligocast_compat.c -lm
clean:
	-rm oligocast

A2X=a2x

docs: oligocast.manual.html oligocast.1

oligocast.manual.html: oligocast.manual.txt
	$(A2X) -d manpage -f xhtml -v oligocast.manual.txt

oligocast.1: oligocast.manual.txt
	$(A2X) -d manpage -f manpage -v oligocast.manual.txt
