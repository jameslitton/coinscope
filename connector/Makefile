CC=gcc
CXX=g++
CFLAGS=-Wall -Iincludes
CXXFLAGS=-Wall -std=c++11 -Iincludes
LDLIBS=-lev -lcrypto
VPATH=src:includes

all: main

main: main.cpp bitcoin.o crypto.o iobuf.o bitcoin_handler.o command_handler.o 

clean:
	rm -rf *~ *.o test main


.PHONY: all clean
