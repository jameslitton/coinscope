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

uint32_t g_msg_id; /* left in network byte order */

struct register_getaddr_message {
	struct message msg;
	struct bitcoin::packed_message getaddr;
} __attribute__((packed));


void register_getaddr(int sock) {
	/* register getaddr message */
	unique_ptr<struct bitcoin::packed_message> getaddr(bitcoin::get_message("getaddr"));
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
	struct register_getaddr_message to_register = { {0, hton((uint32_t)sizeof(bitcoin::packed_message)), BITCOIN_PACKED_MESSAGE }, {0}};
#pragma GCC diagnostic warning "-Wmissing-field-initializers"

	memcpy(&to_register.getaddr, getaddr.get(), sizeof(to_register.getaddr));

	do_write(sock, &to_register, sizeof(to_register));

	/* response is to send back a uint32_t in NBO and that's all */

	if (recv(sock, &g_msg_id, sizeof(g_msg_id), MSG_WAITALL) != sizeof(g_msg_id)) {
		perror("registration read");
		abort();
	}
	cout << "registered message at id " << ntoh(g_msg_id) << endl;
}


void send_getaddr(uint32_t nw_handle_id, int fd) {
	constexpr size_t total_len = sizeof(struct message) + sizeof(struct command_msg) + 4; /* room for one target */
	uint8_t buffer[total_len];
	struct message *msg = (struct message *) buffer;
	struct command_msg *cmsg = (struct command_msg*) ((uint8_t*) msg + sizeof(struct message));
	msg->version = 0;
	msg->length = hton((uint32_t)sizeof(*cmsg) + 4);
	msg->message_type = COMMAND;

	cmsg->command = COMMAND_SEND_MSG;
	cmsg->message_id = g_msg_id; /* already in NBO */
	cmsg->target_cnt = hton(1);
#pragma GCC diagnostic ignored "-Warray-bounds"
	cmsg->targets[0] = nw_handle_id; /* already in NBO */
#pragma GCC diagnostic warning "-Warray-bounds"

	do_write(fd, msg, total_len);
}


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

	int control = unix_sock_client((const char*)cfg->lookup("connector.control_path"), false);


	map<uint32_t, size_t> getaddrs_remaining;
	mutex g_mux;


	register_getaddr(control);

	get_all_cxn(control, [&](struct connection_info *info, size_t ) {
			getaddrs_remaining[info->handle_id] = 16;
		});

	cout << "set " << getaddrs_remaining.size() << " connections\n";

	
	bcwatch watcher(bc_client, 
	                [](unique_ptr<struct bc_channel_msg>) { /* ignore new people */},
	                [&](unique_ptr<struct bc_channel_msg> bc_msg) {
		                lock_guard<mutex> lock(g_mux);
		                getaddrs_remaining.erase(hton(bc_msg->handle_id)); /* bcwatch puts into host order, but we didn't fix it in getaddrs */
	                });

	thread bc_thread([&]() { watcher.loop_forever(); });
	bc_thread.detach();

	for(auto &p : getaddrs_remaining) {
		send_getaddr(p.first, control);
		--(p.second);
	}

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
				const struct log_format *log = (const struct log_format *) input_buf.extract_buffer().const_ptr();
				
				uint32_t nw_handle_id = *((uint32_t*)(log->rest + 0));
				bool is_sender = *((bool*) (log->rest + 4));
				const struct bitcoin::packed_message *bc_msg = (const struct bitcoin::packed_message*) (log->rest + 5);
				if (!is_sender && strcmp(bc_msg->command, "addr") == 0) {
					/* check addr payload */
					uint64_t entries = bitcoin::get_varint(bc_msg->payload, NULL);
					lock_guard<mutex> lock(g_mux);
					auto it = getaddrs_remaining.find(nw_handle_id);
					if (it != getaddrs_remaining.end()) {
						if (entries == 0 || it->second == 1) {
							getaddrs_remaining.erase(it);
						} else {
							send_getaddr(nw_handle_id, control);
							if (entries > 1) {
								--(it->second);
							}
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

