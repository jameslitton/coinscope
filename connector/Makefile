CC=gcc
CXX=g++
CFLAGS=-Wall -Iincludes -I../shared/includes
CXXFLAGS=-Wall -std=c++11 -Iincludes -I../shared/includes -g
LDLIBS=-lev -lcrypto
VPATH=../shared/src:src:includes

all: main tester

main: main.cpp bitcoin.o crypto.o iobuf.o bitcoin_handler.o command_handler.o logger.o

tester: bitcoin.o tester.cpp crypto.o logger.o iobuf.o

clean:
	rm -rf *~ *.o main


.PHONY: all clean
