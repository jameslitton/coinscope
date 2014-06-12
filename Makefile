CC=gcc
CXX=g++
CFLAGS=-Wall
CXXFLAGS=-Wall -std=c++11
LDLIBS=-lev

all: test main

test: test.cpp

main: main.cpp bitcoin.cpp

clean:
	rm -rf *~ *.o test main


.PHONY: all clean
