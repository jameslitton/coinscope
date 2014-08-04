LDLIBS=-lcrypto -lconfig++ -lev -lpthread
include ../makefile.defs

all: connect spider 

clean_extra: 
	rm -rf connect spider

connect: ../shared/bitcoin.o ../shared/network.o ../shared/crypto.o ../shared/iobuf.o ../shared/config.o ../shared/logger.o ../shared/write_buffer.o ../shared/wrapped_buffer.o ../shared/alloc_buffer.o ../shared/mmap_buffer.o bcwatch.o ../shared/read_buffer.o

spider: ../shared/bitcoin.o ../shared/network.o ../shared/crypto.o ../shared/iobuf.o ../shared/config.o ../shared/logger.o ../shared/read_buffer.o ../shared/wrapped_buffer.o ../shared/alloc_buffer.o ../shared/mmap_buffer.o ../shared/write_buffer.o bcwatch.o ../shared/read_buffer.o

#addresses: ../shared/bitcoin.o ../shared/network.o ../shared/crypto.o ../shared/iobuf.o ../shared/config.o ../shared/logger.o ../shared/read_buffer.o ../shared/wrapped_buffer.o ../shared/alloc_buffer.o ../shared/mmap_buffer.o

