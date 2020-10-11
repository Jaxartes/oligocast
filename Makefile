oligocast: oligocast.c oligocast_compat.c oligocast_config.h oligocast.h
	cc -Wall -g -o oligocast oligocast.c oligocast_compat.c -lm
clean:
	-rm oligocast
