all: csum_partial chain1.svg

csum_partial: csum_partial.c Makefile
	gcc $(CFLAGS) -O2 -gdwarf-4 -g2  -march=skylake csum_partial.c -o csum_partial
	
	
chain1.svg: graphs/chain1.dot
	dot -Tsvg -O graphs/chain1.dot 
	mv graphs/chain1.dot.svg chain1.svg