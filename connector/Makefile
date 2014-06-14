CC=gcc
CXX=g++
CFLAGS=-Wall
CXXFLAGS=-Wall -std=c++11
LDLIBS=-lev -lcrypto

all: test main

test: test.cpp crypto.o

main: main.cpp bitcoin.o crypto.o iobuf.o bitcoin_handler.o

clean:
	rm -rf *~ *.o test main


.PHONY: all clean
