/* standard C libraries */
#include <cstdlib>
#include <cstring>
#include <cassert>


/* standard C++ libraries */
#include <vector>
#include <set>
#include <random>
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

/* our libraries */
#include "bitcoin.hpp"
#include "bitcoin_handler.hpp"
#include "command_handler.hpp"
#include "iobuf.hpp"
#include "netwrap.hpp"
#include "logger.hpp"

using namespace std;

namespace bc = bitcoin;


int main(int argc, const char *argv[]) {

	g_log(INTERNALS) << "Starting up";
	char control_filename[] = "/tmp/bitcoin_control";
	unlink(control_filename);

	struct sockaddr_un control_addr;
	bzero(&control_addr, sizeof(control_addr));
	strcpy(control_addr.sun_path, control_filename);

	int control_sock = Socket(AF_UNIX, SOCK_STREAM, 0);
	fcntl(control_sock, F_SETFD, O_NONBLOCK);
	Bind(control_sock, (struct sockaddr*)&control_addr, strlen(control_addr.sun_path) + 
	     sizeof(control_addr.sun_family));
	Listen(control_sock, 5);

	ev::default_loop loop;
	ctrl::accept_handler ctrl_handler(control_sock);
	

	g_log(INTERNALS) << "Entering event loop";
	while(true) {
		/* add timer to clean destruction queues */
		/* add timer to attempt recreation of lost control channel */
		/* add timer to recreate lost logging channel */
		loop.run();
	}
	
	g_log(INTERNALS) << "Orderly shutdown";
	return EXIT_SUCCESS;
}
