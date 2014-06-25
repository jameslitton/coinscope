all:
	make all -C logserver
	make all -C connector
	make all -C logclient

clean:
	make clean -C logserver
	make clean -C connector
	make clean -C logclient

.PHONY: all clean
