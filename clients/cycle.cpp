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

/* Cycle through all the active connections with the following policy:

   If the connection originated from us (our local port is ephemeral),
   disconnect and immediately attempt to reconnect.

   If the connection originated external to us, attempt to
   connect. Upon success, disconnect old connection. 
*/

struct outgoing_message {
	wrapped_buffer<uint8_t> buffer;
	size_t length;
};

struct outgoing_message connect_msg(const struct sockaddr_in &remote) {
	wrapped_buffer<uint8_t> buf(sizeof(struct connect_message));
	struct connect_message *message = (struct connect_message*) buf.ptr();

	message->version = 0; 
	message->length = hton((uint32_t)sizeof(connect_payload));
	message->message_type = CONNECT;
	message->payload.local_addr.sin_family = AF_INET;
	message->payload.local_addr.sin_addr.s_addr = 0; /* current version of connector ignores this */
	message->payload.local_addr.sin_port = 0;

	memcpy(&message->payload.remote_addr, &remote, sizeof(remote));
	struct outgoing_message rv = { buf, sizeof(*message) };
	return rv;
}

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

typedef set<struct sockaddr_in, addr_cmp> addr_set_t;
typedef map<struct sockaddr_in, uint32_t, addr_cmp> addr_handle_map_t;

int main(int argc, char *argv[]) {

	if (argc == 2) {
		load_config(argv[1]);
	} else {
		load_config("../netmine.cfg");
	}

	const libconfig::Config *cfg(get_config());

	string root((const char*)cfg->lookup("logger.root"));
	string client_dir = root + "clients/";


	addr_set_t bound_addrs;

	libconfig::Setting &list = cfg->lookup("connector.bitcoin.listeners");
	for(int index = 0; index < list.getLength(); ++index) {
		libconfig::Setting &setting = list[index];
		string family((const char*)setting[0]);
		string ipv4((const char*)setting[1]);
		uint16_t port((int)setting[2]);
		struct sockaddr_in bound_addr;
		bzero(&bound_addr, sizeof(bound_addr));
		bound_addr.sin_family = AF_INET;
		bound_addr.sin_port = htons(port);
		if (inet_pton(AF_INET, ipv4.c_str(), &bound_addr.sin_addr) != 1) {
			cerr << "Bad address format on address " <<  index << ": " << strerror(errno) << endl;
			abort();
		}
		bound_addrs.insert(bound_addr);
	}

	addr_handle_map_t incoming_cxn;
	addr_handle_map_t outgoing_cxn;

	int sock = unix_sock_client((const char*)cfg->lookup("connector.control_path"), false);

	get_all_cxn(sock, [&](struct connection_info *info, size_t ) {
			if (bound_addrs.find(info->local_addr) != bound_addrs.end()) {
				/* local address is bound address, so they connected to us */
			  cout << "Locally connected to " << info->remote_addr << " inferred from " << info->local_addr << endl;
				incoming_cxn[info->remote_addr] = info->handle_id;
			} else {
				outgoing_cxn[info->remote_addr] = info->handle_id;
			}
		});

	size_t cnt = 0;

	for(auto &p : outgoing_cxn) { /* disconnect reconnect */
	  cnt++;
	  //cout << "Cycling " << ntoh(p.second) << endl;
	  struct outgoing_message disconn(disconnect_msg(p.second));
	  do_write(sock, disconn.buffer.const_ptr(), disconn.length);	  
	  struct outgoing_message conn(connect_msg(p.first));
	  do_write(sock, conn.buffer.const_ptr(), conn.length);
	}
	cout << "cycled " << cnt << " handles\n";

	/* capture any new bitcoin messages before next part */
	int bitcoin_client = unix_sock_client(client_dir + "bitcoin", false);

	addr_set_t remaining;
	for(auto &p : incoming_cxn) { /* double connect */
		struct outgoing_message conn(connect_msg(p.first));
		do_write(sock, conn.buffer.const_ptr(), conn.length);
		remaining.insert(p.first);
	}

	/* wait for notices, upon success disconnect old one. Upon failure leave it alone */
	bcwatchers::bcwatch watcher(bitcoin_client, 
	                [&](unique_ptr<bc_channel_msg> bc_msg) { 
		                auto cnt = remaining.erase(bc_msg->remote_addr);
		                if (cnt > 0) {
				  cout << "Denatted " << bc_msg->remote_addr << endl;
				  struct outgoing_message disconn(disconnect_msg(incoming_cxn[bc_msg->remote_addr]));
				  do_write(sock, disconn.buffer.const_ptr(), disconn.length);		
		                }
	                },
	                [&](unique_ptr<bc_channel_msg> bc_msg) { 
		                cout << "Could not reach " << bc_msg->remote_addr << endl;
		                remaining.erase(bc_msg->remote_addr);
	                });
	while(remaining.size()) {
		watcher.loop_once();
	}
	return EXIT_SUCCESS;
};

