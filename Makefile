CC=g++
CCFLAGS=-L./cppNBT -lz -lpng -lcppnbt -I. -I./cppNBT/src

rendermap: rendermap.cc color_data.1.7.hh color_data.1.8.hh color_data.orig.hh cppNBT/libcppnbt.a
	$(CC) $(CCFLAGS) rendermap.cc -lcppnbt -lpng -lz -o rendermap

cppNBT/libcppnbt.a:
	make -C cppNBT
