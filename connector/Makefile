CC=gcc
CXX=g++
CFLAGS=-Wall -Iincludes -I../shared/includes
CXXFLAGS=-Wall -std=c++11 -Iincludes -I../shared/includes -g
LDLIBS=-lev -lcrypto
VPATH=../shared/src:src:includes

all: main 

main: main.cpp bitcoin.o crypto.o iobuf.o bitcoin_handler.o command_handler.o logger.o

clean:
	rm -rf *~ *.o main


.PHONY: all clean
