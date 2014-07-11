include ../makefile.defs

SHARED=../shared/bitcoin.o ../shared/crypto.o ../shared/iobuf.o ../shared/logger.o ../shared/config.o ../shared/network.o

LDLIBS=-lev -lcrypto -lconfig++

all: main 

main: main.cpp bitcoin_handler.o command_handler.o $(SHARED)

