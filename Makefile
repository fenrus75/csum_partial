all: csum_partial

csum_partial: csum_partial.c Makefile
	gcc $(CFLAGS) -O2 -gdwarf-4 -g2  -march=skylake csum_partial.c -o csum_partial
	