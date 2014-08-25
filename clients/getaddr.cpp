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
	cmsg->target_cnt = 1;
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


int main(int argc, char *argv[]) {

	if (argc == 2) {
		load_config(argv[1]);
	} else {
		load_config("../netmine.cfg");
	}

	const libconfig::Config *cfg(get_config());

	string root((const char*)cfg->lookup("logger.root"));
	string client_dir = root + "clients/";
	int bc_client = unix_sock_client(client_dir + "bitcoin", false);

	int sock = unix_sock_client((const char*)cfg->lookup("connector.control_path"), false);


	map<uint32_t, size_t> getaddrs_remaining;
	mutex g_mux;


	get_all_cxn(sock, [&](struct connection_info *info, size_t ) {
			getaddrs_remaining[info->handle_id] = 8;
		});


	bcwatch watcher(bc_client, 
	                [](unique_ptr<struct bc_channel_msg>) { /* ignore new people */},
	                [&](unique_ptr<struct bc_channel_msg> bc_msg) {
		                lock_guard<mutex> lock(g_mux);
		                getaddrs_remaining.erase(hton(bc_msg->handle_id)); /* bcwatch puts into host order, but we didn't fix it in getaddrs */
	                });

	thread bc_thread([&]() { watcher.loop_forever(); });
	bc_thread.detach();

	int bc_msg_client = unix_sock_client(client_dir + "bitcoin_msg", false);
	read_buffer input_buf(sizeof(uint32_t));
	bool reading_len = true;
	while(true) {
		auto ret = input_buf.do_read(bc_msg_client);
		int r = ret.first;
		if (r == 0) {
			cerr << "Disconnected\n";
			exit(0);
		} else if (r < 0) {
			cerr << "Got error, " << strerror(errno) << endl;
			abort();
		}

		if (!input_buf.hungry()) {
			if (reading_len) {
				uint32_t netlen = *((const uint32_t*) input_buf.extract_buffer().const_ptr());
				input_buf.cursor(0);
				input_buf.to_read(ntoh(netlen));
				reading_len = false;
			} else {
				const uint8_t *buf = input_buf.extract_buffer().const_ptr();
				const uint8_t *msg = buf + 8 + 1;
				uint32_t nw_handle_id = *((uint32_t*) msg);
				bool is_sender = *((bool*) (msg+4));
				const struct bitcoin::packed_message *bc_msg = (const struct bitcoin::packed_message*) (msg + 5);
				if (!is_sender && strcmp(bc_msg->command, "addr") == 0) {
					/* check addr payload */
					uint64_t entries = bitcoin::get_varint(bc_msg->payload, NULL);
					lock_guard<mutex> lock(g_mux);
					auto it = getaddrs_remaining.find(nw_handle_id);
					if (it != getaddrs_remaining.end()) {
						if (entries == 0 || it->second == 1) {
							getaddrs_remaining.erase(it);
						} else {
							--(it->second);
						}
					}
				}


				input_buf.cursor(0);
				input_buf.to_read(4);
				reading_len = true;
			}
		}

		{
			lock_guard<mutex> lock(g_mux);
			if (getaddrs_remaining.size() == 0) {
				break;
			}
		}
	}

	
	

	return EXIT_SUCCESS;
};

