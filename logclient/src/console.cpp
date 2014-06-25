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
#include "iobuf.hpp"

using namespace std;

int main(int argc, char *argv[]) {
	char path[] = "/tmp/logger/clients";
	struct sockaddr_un addr;
	bzero(&addr, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, path);

	int client = Socket(AF_UNIX, SOCK_STREAM, 0);
	Connect(client, (struct sockaddr*)&addr, strlen(addr.sun_path) + 
	     sizeof(addr.sun_family));

	bool reading_len(true);
	uint32_t to_read(sizeof(uint32_t));

	iobuf input_buf;

	while(true) {
		input_buf.grow(to_read);

		ssize_t r = read(client, input_buf.offset_buffer(), to_read);
		if (r > 0) {
			to_read -= r;
		} else if (r == 0) {
			cerr << "Disconnected\n";
			return EXIT_SUCCESS;
		} else if (r < 0) {
			cerr << "Got error, " << strerror(errno) << endl;
			return EXIT_FAILURE;
		}

		if (to_read == 0) {
			if (reading_len) {
				uint32_t netlen = *((uint32_t*)input_buf.raw_buffer());
				to_read = ntohl(netlen);
				input_buf.seek(0);
			} else {
				cout.write((const char*)input_buf.raw_buffer(), input_buf.location());
				cout << endl;
				input_buf.seek(0);
				to_read = 4;
				reading_len = true;
			}
		}
	}

}
