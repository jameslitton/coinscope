include ../makefile.defs

SHARED=../shared/bitcoin.o ../shared/crypto.o ../shared/iobuf.o ../shared/logger.o ../shared/config.o ../shared/network.o ../shared/read_buffer.o ../shared/write_buffer.o ../shared/mmap_buffer.o ../shared/alloc_buffer.o ../shared/wrapped_buffer.o

LDLIBS=-lev -lcrypto -lconfig++ -lboost_program_options

all: main 

clean_extra: 
	rm -rf main

main: main.cpp bitcoin_handler.o command_handler.o $(SHARED)

