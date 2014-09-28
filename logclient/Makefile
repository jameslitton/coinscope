include ../makefile.defs
LDLIBS=-lev -lconfig++ -lboost_program_options

all: console verbatim

clean_extra:
	rm -rf console verbatim

console: console.cpp ../shared/iobuf.o ../shared/logger.o ../shared/config.o ../shared/network.o ../shared/read_buffer.o ../shared/wrapped_buffer.o ../shared/alloc_buffer.o ../shared/mmap_buffer.o ../shared/write_buffer.o

verbatim: verbatim.cpp ../shared/iobuf.o ../shared/logger.o ../shared/config.o ../shared/network.o ../shared/read_buffer.o ../shared/wrapped_buffer.o ../shared/alloc_buffer.o ../shared/mmap_buffer.o ../shared/write_buffer.o

