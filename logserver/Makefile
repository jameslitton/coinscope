include ../makefile.defs

LDLIBS=-lev -lconfig++ -lboost_program_options

all: main

clean_extra: 
	rm -rf main

main: main.cpp collector.o input_cxn.o output_cxn.o ../shared/network.o ../shared/config.o ../shared/wrapped_buffer.o ../shared/alloc_buffer.o ../shared/mmap_buffer.o ../shared/read_buffer.o ../shared/write_buffer.o

