include ../makefile.defs

LDLIBS=-lev -lconfig++

all: main

clean_extra: 
	rm -rf main

main: main.cpp collector.o input_cxn.o output_cxn.o ../shared/iobuf.o ../shared/network.o ../shared/config.o

