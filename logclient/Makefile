include ../makefile.defs
LDLIBS=-lev -lconfig++

all: console

clean_extra:
	rm -rf console

console: console.cpp ../shared/iobuf.o ../shared/logger.o ../shared/config.o ../shared/network.o ../shared/read_buffer.o ../shared/wrapped_buffer.o ../shared/alloc_buffer.o ../shared/mmap_buffer.o

