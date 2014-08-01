LDLIBS=-lcrypto -lconfig++ -lev
include ../makefile.defs

all: connect

clean_extra: 
	rm -rf connect

connect: ../shared/bitcoin.o ../shared/network.o ../shared/crypto.o ../shared/iobuf.o ../shared/config.o ../shared/logger.o

