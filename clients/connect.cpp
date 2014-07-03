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


#include "netwrap.hpp"
#include "command_structures.hpp"
#include "bitcoin.hpp"

/* This is a simple test client to demonstrate use of the connector */


using namespace std;

struct connect_message {
	uint32_t length;
	uint32_t message_type;
	struct connect_payload payload;
} __attribute__((packed));

int main(int/* argc*/, char **/*argv*/) {
	char command_path[] = CONTROL_PATH;
	
	struct connect_message message = { sizeof(connect_message), CONNECT };
	
	
	return EXIT_SUCCESS;
};
