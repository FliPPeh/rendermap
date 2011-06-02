CC=g++
CCFLAGS=-L. -lz -lpng -lcppnbt -I.

rendermap: rendermap.cc
	$(CC) $(CCFLAGS) rendermap.cc -lz -lcppnbt -lpng -o rendermap
