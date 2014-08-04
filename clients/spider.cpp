/* standard C libraries */
#include <cstdlib>
#include <cstring>
#include <cassert>

/* standard C++ libraries */
#include <iostream>
#include <utility>
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


#include "network.hpp"
#include "command_structures.hpp"
#include "bitcoin.hpp"
#include "config.hpp"
#include "logger.hpp"
#include "read_buffer.hpp"
#include "bcwatch.hpp"

/* This is a simple test client to demonstrate use of the connector */


using namespace std;
using namespace ctrl;

size_t g_successful_connects = 0;

uint32_t g_msg_id; /* left in network byte order */

mutex g_visit_mux;
condition_variable g_visit_cond;

void watch_cxn(const libconfig::Config *cfg);
void fetch_addrs(const libconfig::Config *cfg);

struct connect_message {
	uint8_t version;
	uint32_t length;
	uint8_t message_type;
	struct connect_payload payload;
} __attribute__((packed));

inline void do_write(int fd, const void *ptr, size_t len) {
	size_t so_far = 0;
	do {
		ssize_t r = write(fd, (const char *)ptr + so_far, len - so_far);
		if (r > 0) {
			so_far += r;
		} else if (r < 0) {
			assert(errno != EAGAIN && errno != EWOULDBLOCK); /* don't use this with non-blockers */
			if (errno != EINTR) {
				cerr << strerror(errno) << endl;
				abort();
			}
		}
	} while ( so_far < len);
}


deque<pair<wrapped_buffer<uint8_t>, size_t > > pending_messages;

struct pvp_comp {
	bool operator()(const struct sockaddr_in &lhs, const struct sockaddr_in &rhs) {
		/* byte order doesn't matter, just consistency */
		int t = lhs.sin_addr.s_addr - rhs.sin_addr.s_addr;
		if (!t) {
			t = lhs.sin_port - rhs.sin_port;
		}
		return t < 0;
	}
};

set<struct sockaddr_in, pvp_comp> visited;

struct register_getaddr_message {
	struct message msg;
	struct bitcoin::packed_message getaddr;
} __attribute__((packed));

int main(int argc, char *argv[]) {

	if (argc == 2) {
		load_config(argv[1]);
	} else {
		load_config("../netmine.cfg");
	}

	const libconfig::Config *cfg(get_config());

	int sock = unix_sock_client((const char*)cfg->lookup("connector.control_path"), false);



	/* register getaddr message */
	{
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
	

	wrapped_buffer<uint8_t> buf(sizeof(struct connect_message));
	struct connect_message *message = (struct connect_message*) buf.ptr();
	
	message->version = 0; 
	message->length = hton((uint32_t)sizeof(connect_payload));
	message->message_type = CONNECT;

	message->payload.remote_addr.sin_family = AF_INET;
	if (inet_pton(AF_INET, "127.0.0.1", &message->payload.remote_addr.sin_addr) != 1) {
		perror("inet_pton destination");
		return EXIT_FAILURE;
	}

	message->payload.local_addr.sin_family = AF_INET;
	if (inet_pton(AF_INET, "127.0.0.1", &message->payload.local_addr.sin_addr) != 1) {
		perror("inet_pton source");
		return EXIT_FAILURE;
	}

	message->payload.remote_addr.sin_port = hton(static_cast<uint16_t>(8333));
	message->payload.local_addr.sin_port = hton(static_cast<uint16_t>(0xdead));


	pending_messages.push_back(make_pair(buf, sizeof(struct connect_message)));

	/* okay, start up worker thread */

	thread cxn_watcher(watch_cxn, cfg);
	thread fetcher(fetch_addrs, cfg);
	cxn_watcher.detach();
	fetcher.detach();


	/* this is a lazy spider. It assumes we have a local bitcoin guy to start
	   with and sets it as the start. It would be pretty easy to set
	   this up to pick these guys off of the DNS. Just being lazy so I
	   can watch the initiation */

	while(true) {
		// can starve a little bit
		unique_lock<mutex> lock(g_visit_mux);
		g_visit_cond.wait(lock, [&]{ return pending_messages.size(); });
		while(pending_messages.size()) {


			auto pmf = pending_messages.front();
			pending_messages.pop_front();

			lock.unlock();

			const struct message *msg = (const struct message*) pmf.first.const_ptr();
			if (msg->message_type == CONNECT) {
				const struct connect_message *con_msg  = (const struct connect_message*) msg;
				cout << "Connect attemp to " << *((struct sockaddr*) &con_msg->payload.remote_addr) << endl;
				auto p = visited.insert(con_msg->payload.remote_addr);
				if (p.second) { /* Did not attempt to connect to him yet */
					assert(pmf.second == sizeof(*con_msg));
					do_write(sock, con_msg, pmf.second);

				}
			} else {
				/* some other kind of message, just send it */
				do_write(sock, msg , pmf.second);
			}


			lock.lock();
		}

	}

	return EXIT_SUCCESS;
};

bool is_private(uint32_t ip) {
  uint32_t a = ip & 0x000000FF;
  uint32_t b = ip & 0x0000FF00;

  return (a == 10 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31));
}


void handle_message(read_buffer &input_buf) {
	const uint8_t *buf = input_buf.extract_buffer().const_ptr();
	enum log_type lt(static_cast<log_type>(buf[0]));

	if (lt != BITCOIN_MSG) {
		cout << ((char *)buf+8+1) << endl;
		return;
	}


	assert(lt == BITCOIN_MSG);
	//time_t time = ntoh(*( (uint64_t*)(buf+1)));
	
	const uint8_t *msg = buf + 8 + 1;

	//uint32_t id = ntoh(*((uint32_t*) msg));
	bool is_sender = *((bool*) (msg+4));
	uint64_t total_cnt = 0;

	const struct bitcoin::packed_message *bc_msg = (const struct bitcoin::packed_message*) (msg + 5);
	const struct bitcoin::full_packed_net_addr *addrs;
	if (!is_sender && strcmp(bc_msg->command, "addr") == 0) { /* okay, we actually care */
		uint8_t size;
		uint64_t count = bitcoin::get_varint(bc_msg->payload, &size);
		total_cnt += count;
		addrs = (const struct bitcoin::full_packed_net_addr*) (bc_msg->payload + size);

		wrapped_buffer<uint8_t> buf(sizeof(struct connect_message));
	
		struct connect_message *message = (struct connect_message*) buf.ptr(); /* re-trigger COW check */

		message->version = 0; 
		message->length = hton((uint32_t)sizeof(connect_payload));
		message->message_type = CONNECT;

		message->payload.local_addr.sin_family = AF_INET;
		if (inet_pton(AF_INET, "127.0.0.1", &message->payload.local_addr.sin_addr) != 1) {
			perror("inet_pton source");
			abort();
		}


		for(size_t i = 0; i < count; ++i) {
		  if (!is_private(addrs[i].rest.addr.ipv4.as.number) && ntoh(addrs[i].rest.port) == 8333) {
			message = (struct connect_message*) buf.ptr(); /* re-trigger COW check */
			struct sockaddr_in netaddr;
			netaddr.sin_family = AF_INET;
			netaddr.sin_port = addrs[i].rest.port;
			netaddr.sin_addr = addrs[i].rest.addr.ipv4.as.in_addr;
			memcpy(&message->payload.remote_addr, &netaddr, sizeof(netaddr));
			{
				unique_lock<mutex> lck(g_visit_mux);
				auto p(make_pair(buf, sizeof(*message)));
				pending_messages.push_back(p);
			}
		  }

		}
		if (total_cnt) { 
			g_visit_cond.notify_all();
		}
	}
}


void watch_cxn(const libconfig::Config *cfg) {
	string root((const char*)cfg->lookup("logger.root"));
	mkdir(root.c_str(), 0777);
	string client_dir(root + "clients/");
	int bitcoin_client = unix_sock_client(client_dir + "bitcoin", false);
	bcwatch watcher(bitcoin_client, 
	                [](struct bc_channel_msg *bc_msg) {

		                size_t total_len = sizeof(struct message) + sizeof(command_msg) + 4; /* room for one target */
		                wrapped_buffer<uint8_t> buf(total_len);
		                struct message *msg = (struct message *) buf.ptr();
		                struct command_msg *cmsg = (struct command_msg*) ((uint8_t*) msg + sizeof(struct message));
		                msg->version = 0;
		                msg->length = hton((uint32_t)sizeof(*cmsg) + 4);
		                msg->message_type = COMMAND;

		                cmsg->command = COMMAND_SEND_MSG;
		                cmsg->message_id = g_msg_id; /* already in NBO */
		                cmsg->target_cnt = hton(1);
		                cmsg->targets[0] = hton(bc_msg->handle_id);
		                cout << '(' << g_successful_connects << ") successful connect to " << *((struct sockaddr*) &bc_msg->remote) << endl;
		                g_successful_connects += 1;

		                auto p = make_pair(buf, total_len);
		                {
			                unique_lock<mutex> lock(g_visit_mux);
			                pending_messages.push_back(p);
		                }
		                g_visit_cond.notify_all();
	                },
	                [](struct bc_channel_msg *msg) {
		                cout << "Unsuccessful connect. Details follow: " << endl;
		                cout << "\ttime: " << msg->time << endl;
		                cout << "\tupdate_type: " << msg->update_type << endl;
		                cout << "\tremote: " << *((struct sockaddr*) &msg->remote) << endl;
		                cout << "\ttext_length: " << msg->text_length << endl;
		                if (msg->text_length) {
			                cout << "\ttext: " << msg->text << endl;
		                }
	                });
	watcher.loop_forever();
}
	                


void fetch_addrs(const libconfig::Config *cfg) {

	string root((const char*)cfg->lookup("logger.root"));

	mkdir(root.c_str(), 0777);
	string client_dir(root + "clients/");

	int client = unix_sock_client(client_dir + "bitcoin_msg", false);

	bool reading_len(true);

	read_buffer input_buf(sizeof(uint32_t));

	while(true) {
		auto ret = input_buf.do_read(client);
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
				handle_message(input_buf);
				input_buf.cursor(0);
				input_buf.to_read(4);
				reading_len = true;
			}
		}
	}
}
