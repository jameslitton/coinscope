all:
	$(MAKE) all -C logserver
	$(MAKE) all -C connector
	$(MAKE) all -C logclient

clean:
	$(MAKE) clean -C logserver
	$(MAKE) clean -C connector
	$(MAKE) clean -C logclient

.PHONY: all clean
