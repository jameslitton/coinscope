all:
	$(MAKE) all -C logserver
	$(MAKE) all -C connector
	$(MAKE) all -C logclient
	$(MAKE) all -C clients

clean:
	$(MAKE) clean -C logserver
	$(MAKE) clean -C connector
	$(MAKE) clean -C logclient
	$(MAKE) clean -C clients

.PHONY: all clean
