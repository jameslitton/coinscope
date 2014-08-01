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


#include "network.hpp"
#include "command_structures.hpp"
#include "bitcoin.hpp"
#include "config.hpp"

/* This is a simple test client to demonstrate use of the connector */


using namespace std;
using namespace ctrl;

struct connect_message {
	uint8_t version;
	uint32_t length;
	uint8_t message_type;
	struct connect_payload payload;
} __attribute__((packed));

int main(int argc, char *argv[]) {

	if (argc == 2) {
		load_config(argv[1]);
	} else {
		load_config("../netmine.cfg");
	}

	const libconfig::Config *cfg(get_config());

	int sock = unix_sock_client((const char*)cfg->lookup("connector.control_path"), false);

#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
	struct connect_message message = { 0, hton((uint32_t)sizeof(connect_payload)), CONNECT, {{0},{0}} };
#pragma GCC diagnostic warning "-Wmissing-field-initializers"

	if (inet_pton(AF_INET, "127.0.0.1", &message.payload.remote.addr.ipv4.as.in_addr) != 1) {
		perror("inet_pton destination");
		return EXIT_FAILURE;
	}

	if (inet_pton(AF_INET, "127.0.0.1", &message.payload.local.addr.ipv4.as.in_addr) != 1) {
		perror("inet_pton source");
		return EXIT_FAILURE;
	}

	message.payload.remote.port = hton(static_cast<uint16_t>(8333));
	message.payload.local.port = hton(static_cast<uint16_t>(0xdead));

	cout << hton((uint32_t)sizeof(connect_payload)) << endl;
	cout << ntoh(message.length) << endl;

	if (write(sock, &message, sizeof(message)) != sizeof(message)) {
		perror("write");
		return EXIT_FAILURE;
	}
	
	
	return EXIT_SUCCESS;
};
