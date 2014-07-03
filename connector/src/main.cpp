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
#include "network.hpp"

using namespace std;

namespace bc = bitcoin;


int main(int argc, const char *argv[]) {

	g_log<DEBUG>("Starting up");

	char control_filename[] = CONTROL_PATH;
	unlink(control_filename);

	struct sockaddr_un control_addr;
	bzero(&control_addr, sizeof(control_addr));
	control_addr.sun_family = AF_UNIX;
	strcpy(control_addr.sun_path, control_filename);

	int control_sock = Socket(AF_UNIX, SOCK_STREAM, 0);
	fcntl(control_sock, F_SETFL, O_NONBLOCK);
	Bind(control_sock, (struct sockaddr*)&control_addr, strlen(control_addr.sun_path) + 
	     sizeof(control_addr.sun_family));
	Listen(control_sock, 5);

	/* TODO, make configurable! */
	struct sockaddr_in bitcoin_addr;
	bzero(&bitcoin_addr, sizeof(bitcoin_addr));
	bitcoin_addr.sin_family = AF_INET;
	bitcoin_addr.sin_port = htons(8333);
	bitcoin_addr.sin_addr.s_addr = htonl(INADDR_ANY); 
	int bitcoin_sock = Socket(AF_INET, SOCK_STREAM, 0);
	fcntl(bitcoin_sock, F_SETFL, O_NONBLOCK);
   int optval = 1;
   setsockopt(bitcoin_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	Bind(bitcoin_sock, (struct sockaddr*)&bitcoin_addr, sizeof(bitcoin_addr));
	Listen(bitcoin_sock, 128);
	

	ev::default_loop loop;
	ctrl::accept_handler ctrl_handler(control_sock);
	bc::accept_handler bitcoin_handler(bitcoin_sock, bitcoin_addr.sin_addr, bitcoin_addr.sin_port);

	g_log<DEBUG>("Entering event loop");
	while(true) {
		/* add timer to clean destruction queues */
		/* add timer to attempt recreation of lost control channel */
		/* add timer to recreate lost logging channel */
		loop.run();
	}
	
	close(control_sock);
	close(bitcoin_sock);
	g_log<DEBUG>("Orderly shutdown");
	return EXIT_SUCCESS;
}
