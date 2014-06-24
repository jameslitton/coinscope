all:
	make all -C logserver
	make all -C connector

clean:
	make clean -C logserver
	make clean -C logserver

.PHONY: all clean
