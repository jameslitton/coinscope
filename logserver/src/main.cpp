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

#include "network.hpp"
#include "accept_handler.hpp"
#include "input_cxn.hpp"
#include "output_cxn.hpp"

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

int main(int /*argc*/, char ** /*argv*/ ) {

	/* TODO: make configurable */
	mkdir("/tmp/logger", 0777);
	int input_channel = unix_sock_server("/tmp/logger/servers", 5, true);
	int output_channel = unix_sock_server("/tmp/logger/clients", 5, true);

	ev::default_loop loop;

	handlers::accept_handler<output_cxn::handler> out_handler(output_channel);
	handlers::accept_handler<input_cxn::handler> in_handler(input_channel);

	while(true) {
		loop.run();
	}
	close(input_channel);
	close(output_channel);
	return EXIT_SUCCESS;
}

