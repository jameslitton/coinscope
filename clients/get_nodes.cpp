/* standard C libraries */
#include <cstdlib>
#include <cstring>
#include <cassert>

/* standard C++ libraries */
#include <iostream>
#include <utility>
#include <map>
#include <stack>
#include <memory>
#include <set>
#include <mutex>
#include <condition_variable>
#include <thread>

/* standard unix libraries */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>


#include "network.hpp"
#include "command_structures.hpp"
#include "bitcoin.hpp"
#include "config.hpp"
#include "logger.hpp"
#include "lib.hpp"

/* get all existing connections. Write result out to mmapped file (for spider, for instance) and standard out */

using namespace std;
using namespace ctrl;


int main(int argc, char *argv[]) {

	if (argc == 2) {
		load_config(argv[1]);
	} else {
		load_config("../netmine.cfg");
	}

	const libconfig::Config *cfg(get_config());

	int sock = unix_sock_client((const char*)cfg->lookup("connector.control_path"), false);

	uint32_t alloc_size = sizeof(struct message) + sizeof(struct command_msg);
	struct message *msg = (struct message*) ::operator new(alloc_size);
	bzero(msg, sizeof(*msg));

	msg->version = 0;
	msg->length = hton((uint32_t)sizeof(struct command_msg));
	msg->message_type = COMMAND;
	struct command_msg *cmsg = (struct command_msg*) &msg->payload;
	bzero(cmsg, sizeof(*cmsg));
	cmsg->command = COMMAND_GET_CXN;

	do_write(sock, msg, alloc_size);
	cout << "just wrote " << alloc_size << " bytes\n";

	delete msg;


	uint32_t len;
	if (recv(sock, &len, sizeof(len), MSG_WAITALL) != sizeof(len)) {
		throw runtime_error(strerror(errno));
	}
	len = ntoh(len);

	cout << "Got length of " << len << endl;

	cout << "cisizeof " << sizeof(struct connection_info) << endl;
	assert((len*1.0) / sizeof(struct connection_info) - (len / sizeof(struct connection_info)) == 0);
	size_t addresses = len / sizeof(struct connection_info);
	if (addresses == 0) {
		cout << "no addresses\n";
		return 0;
	}

	cout << "Expecting " << addresses << " addresses" << endl;

	size_t mult = 0;
	size_t page_size = sysconf(_SC_PAGE_SIZE);

	while(++mult * page_size < addresses * sizeof(struct sockaddr_in));

	char tmpname[] = "/tmp/nodelist-XXXXXX";
	int fd = mkstemp(tmpname);
	if (fd < 0) {
		throw runtime_error(strerror(errno));
	}

	if (ftruncate(fd, mult * page_size) != 0) {
		throw runtime_error(strerror(errno));
	}
	struct sockaddr_in *data = (struct sockaddr_in*) mmap(NULL, mult * page_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	void * start = data;
	close(fd);

	while(len > 0) {
		struct connection_info info;
		cerr << "Doing recv for " << sizeof(info) << " bytes" << endl;
		if (recv(sock, &info, sizeof(info), MSG_WAITALL) != sizeof(info)) {
			throw runtime_error(strerror(errno));
		} else {
			cout << "handle_id: " << info.handle_id << endl;
			cout << "remote: " << *((struct sockaddr*)&info.remote_addr) << endl;
			cout << "local: " << *((struct sockaddr*)&info.local_addr) << endl;
			cout << "------------------------------------------------------------------------\n";
			memcpy(data++, &info.remote_addr, sizeof(info.remote_addr));
		}
		len -= sizeof(info);
	}

	munmap(start, mult*page_size);

	close(sock);



}
