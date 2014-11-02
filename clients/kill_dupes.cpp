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
#include <thread>
#include <queue>
#include <atomic>

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
#include "read_buffer.hpp"
#include "bcwatch.hpp"
#include "lib.hpp"


using namespace std;
using namespace ctrl;


struct outgoing_message {
	wrapped_buffer<uint8_t> buffer;
	size_t length;
};


struct outgoing_message disconnect_msg(uint32_t nw_handle_id) {
	uint32_t payload_sz = sizeof(struct command_msg) + 4;
	wrapped_buffer<uint8_t> buf(sizeof(struct message) + payload_sz);
	struct message *msg = (struct message*) buf.ptr();
	struct command_msg *cmsg = (struct command_msg*) (buf.ptr() + sizeof(*msg));
	msg->version = 0; 
	msg->length = hton(payload_sz);
	msg->message_type = COMMAND;
	cmsg->command = COMMAND_DISCONNECT;
	cmsg->message_id = 0;
	cmsg->target_cnt = hton((uint32_t)1);
	cmsg->targets[0] = nw_handle_id;
	struct outgoing_message rv = { buf, sizeof(struct message) + payload_sz };
	return rv;
}


struct addr_cmp {
	bool operator()(const struct sockaddr_in &lhs, const struct sockaddr_in &rhs) {
		int t = lhs.sin_addr.s_addr - rhs.sin_addr.s_addr;
		if (!t) {
			t = lhs.sin_port - rhs.sin_port;
		}
		return t < 0;
	}
};

typedef map<struct sockaddr_in, uint32_t, addr_cmp> addr_handle_map_t;

int main(int argc, char *argv[]) {

	if (argc == 2) {
		load_config(argv[1]);
	} else {
		load_config("../netmine.cfg");
	}

	const libconfig::Config *cfg(get_config());


	map<struct sockaddr_in, vector<uint32_t>, addr_cmp> cxn;
	int sock = unix_sock_client((const char*)cfg->lookup("connector.control_path"), false);

	get_all_cxn(sock, [&](struct connection_info *info, size_t ) {
				cxn[info->remote_addr].push_back(info->handle_id);
		});

	size_t cnt(0);

	for(auto &c : cxn) {
	  if (c.second.size() > 1) {
	    uint32_t max_id = 0; //always disconnect oldest if a dupe exists.
	    for(size_t i = 0; i < c.second.size(); ++i) {
	      max_id = max(max_id, ntoh(c.second[i]));
	    }
	    for(size_t i = 0; i < c.second.size(); ++i) {
	      if (max_id > ntoh(c.second[i])) {
		cnt++;
		struct outgoing_message disconn(disconnect_msg(c.second[i]));
		do_write(sock, disconn.buffer.const_ptr(), disconn.length);
	      }
	    }
	  }

	}

	cout << "disconnected " << cnt << " handles\n";

	return EXIT_SUCCESS;
};

