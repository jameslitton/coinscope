CC=gcc
CXX=g++
CFLAGS=-Wall -Iincludes -I../shared/includes
CXXFLAGS=-Wall -std=c++11 -Iincludes -I../shared/includes -g
LDLIBS=-lev 
VPATH=src:includes

all: main

main: main.cpp collector.o

clean: 
	rm -rf *~ *.o main

.PHONY: all clean
