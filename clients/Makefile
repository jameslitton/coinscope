LDLIBS=-lcrypto -lconfig++ -lev -lpthread
include ../makefile.defs

all: connect spider get_nodes cycle getaddr

clean_extra: 
	rm -rf connect spider get_nodes

connect: ../shared/bitcoin.o ../shared/network.o ../shared/crypto.o ../shared/config.o ../shared/logger.o ../shared/write_buffer.o ../shared/wrapped_buffer.o ../shared/alloc_buffer.o ../shared/mmap_buffer.o bcwatch.o ../shared/read_buffer.o ../shared/connector.o

getaddr: ../shared/bitcoin.o ../shared/network.o ../shared/crypto.o ../shared/config.o ../shared/logger.o ../shared/wrapped_buffer.o ../shared/alloc_buffer.o ../shared/mmap_buffer.o ../shared/write_buffer.o bcwatch.o ../shared/read_buffer.o

cycle: ../shared/bitcoin.o ../shared/network.o ../shared/crypto.o ../shared/config.o ../shared/logger.o ../shared/wrapped_buffer.o ../shared/alloc_buffer.o ../shared/mmap_buffer.o ../shared/write_buffer.o bcwatch.o ../shared/read_buffer.o

spider: ../shared/bitcoin.o ../shared/network.o ../shared/crypto.o ../shared/config.o ../shared/logger.o ../shared/wrapped_buffer.o ../shared/alloc_buffer.o ../shared/mmap_buffer.o ../shared/write_buffer.o bcwatch.o ../shared/read_buffer.o

get_nodes: ../shared/bitcoin.o ../shared/network.o ../shared/crypto.o ../shared/config.o ../shared/logger.o ../shared/read_buffer.o ../shared/wrapped_buffer.o ../shared/alloc_buffer.o ../shared/mmap_buffer.o ../shared/write_buffer.o bcwatch.o ../shared/read_buffer.o 

#addresses: ../shared/bitcoin.o ../shared/network.o ../shared/crypto.o ../shared/iobuf.o ../shared/config.o ../shared/logger.o ../shared/read_buffer.o ../shared/wrapped_buffer.o ../shared/alloc_buffer.o ../shared/mmap_buffer.o

