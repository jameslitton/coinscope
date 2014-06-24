/* standard C libraries */
#include <cstdlib>
#include <cstring>
#include <cassert>

/* standard C++ libraries */
#include <iostream>
#include <utility>

/* standard unix libraries */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

/* third party libraries */
#include <ev++.h>

#include "netwrap.hpp"

using namespace std;

int setup_usock(const char *path) {
	unlink(path);

	struct sockaddr_un addr;
	bzero(&addr, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, path);

	int sock = Socket(AF_UNIX, SOCK_STREAM, 0);
	fcntl(sock, F_SETFD, O_NONBLOCK);
	Bind(sock, (struct sockaddr*)&addr, strlen(addr.sun_path) + 
	     sizeof(addr.sun_family));
	Listen(sock, 5);
	return sock;
}

int main(int argc, char *argv[]) {

	/* TODO: make configurable */
	mkdir("/tmp/logger", 0777);
	int input_channel = setup_usock("/tmp/logger/servers");
	int output_channel = setup_usock("/tmp/logger/clients");

}

