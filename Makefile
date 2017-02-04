CC=g++
CCFLAGS=-L./cppNBT -lz -lpng -lcppnbt -I. -I./cppNBT/src

rendermap: rendermap.cc
	$(CC) $(CCFLAGS) rendermap.cc -lcppnbt -lpng -lz -o rendermap
