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

/* This is a simple test client to demonstrate use of the connector */


using namespace std;
using namespace ctrl;

uint32_t g_msg_id; /* left in network byte order */

mutex g_visit_mux;
condition_variable g_visit_cond;

void fetch_addrs(const libconfig::Config *cfg);

struct connect_message {
	uint8_t version;
	uint32_t length;
	uint8_t message_type;
	struct connect_payload payload;
} __attribute__((packed));



stack<struct connect_message> to_visit;

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

/* connect to someone, call getaddr or whatever. When we read as a log
   client we add more people to the visit queue */

void visit(int sock, const struct connect_message &m) { 
	if (write(sock, &m, sizeof(m)) != sizeof(m)) {
		perror("write connect");
		abort();
	}

	struct connect_response response;
	if (recv(sock, &response, sizeof(response), MSG_WAITALL) != sizeof(response)) {
		perror("read connect");
		abort();
	}

	cout << "Got a response, here it is: " << endl;


	cout << "\tResult: " << ntoh(response.result) << endl;
	cout << "\tRegistration ID: " << ntoh(response.registration_id) << endl;
	cout << "\tHandle ID: " << ntoh(response.info.handle_id) << endl;
	cout << "\tremote: " << *((struct sockaddr*)&response.info.remote_addr) << endl;
	cout << "\tlocal: " << *((struct sockaddr*)&response.info.local_addr) << endl;

	if (response.result == 0) {
		ssize_t total_len = sizeof(struct message) + sizeof(command_msg) + 4; /* room for one target */
		struct message *msg = (struct message *) alloca(total_len); 
		struct command_msg *cmsg = (struct command_msg*) ((uint8_t*) msg + sizeof(struct message));
		msg->version = 0;
		msg->length = hton((uint32_t)sizeof(*cmsg) + 4);
		msg->message_type = COMMAND;

		cmsg->command = COMMAND_SEND_MSG;
		cmsg->message_id = g_msg_id; /* already in NBO */
		cmsg->target_cnt = hton(1);
		cmsg->targets[0] = response.info.handle_id; /* already in NBO */

		cout << "Sending command message of " << total_len << " bytes\n";

		if (write(sock, msg, total_len) != total_len) {
			perror("sending command message");
			abort();
		}
	}

}

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

		if (write(sock, &to_register, sizeof(to_register)) != sizeof(to_register)) {
			perror("registration write");
			abort();
		}

		/* response is to send back a uint32_t in NBO and that's all */

		if (recv(sock, &g_msg_id, sizeof(g_msg_id), MSG_WAITALL) != sizeof(g_msg_id)) {
			perror("registration read");
			abort();
		}
		cout << "registered message at id " << ntoh(g_msg_id) << endl;
	}
	


#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
	struct connect_message message = { 0, hton((uint32_t)sizeof(connect_payload)), CONNECT, {{0},{0}} };
#pragma GCC diagnostic warning "-Wmissing-field-initializers"

	message.payload.remote_addr.sin_family = AF_INET;
	if (inet_pton(AF_INET, "127.0.0.1", &message.payload.remote_addr.sin_addr) != 1) {
		perror("inet_pton destination");
		return EXIT_FAILURE;
	}

	message.payload.local_addr.sin_family = AF_INET;
	if (inet_pton(AF_INET, "127.0.0.1", &message.payload.local_addr.sin_addr) != 1) {
		perror("inet_pton source");
		return EXIT_FAILURE;
	}

	message.payload.remote_addr.sin_port = hton(static_cast<uint16_t>(8333));
	message.payload.local_addr.sin_port = hton(static_cast<uint16_t>(0xdead));


	to_visit.push(message);


	/* okay, start up worker thread */

	thread fetcher(fetch_addrs, cfg);
	fetcher.detach();


	/* this is a lazy spider. It assumes we have a local bitcoin guy to start
	   with and sets it as the start. It would be pretty easy to set
	   this up to pick these guys off of the DNS. Just being lazy so I
	   can watch the initiation */

	while(true) {
		// can starve a little bit
		unique_lock<mutex> lock(g_visit_mux);
		g_visit_cond.wait(lock, [&]{ return to_visit.size(); });
		do {
			struct connect_message m(to_visit.top());
			to_visit.pop();
			lock.unlock();

			auto p = visited.insert(m.payload.remote_addr);
			if (p.second) { /* item was not already visited */
				visit(sock, m);
			}

			lock.lock();
		} while(to_visit.size());
	}

	return EXIT_SUCCESS;
};


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

#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
		struct connect_message message = { 0, hton((uint32_t)sizeof(connect_payload)), CONNECT, {{0},{0}} };
#pragma GCC diagnostic warning "-Wmissing-field-initializers"

		message.payload.local_addr.sin_family = AF_INET;
		inet_pton(AF_INET, "127.0.0.1", &message.payload.local_addr.sin_addr);
		message.payload.local_addr.sin_port = hton(static_cast<uint16_t>(0xdead));

		for(size_t i = 0; i < count; ++i) {
			struct sockaddr_in netaddr;
			netaddr.sin_family = AF_INET;
			netaddr.sin_port = addrs[i].rest.port;
			netaddr.sin_addr = addrs[i].rest.addr.ipv4.as.in_addr;
			memcpy(&message.payload.remote_addr, &netaddr, sizeof(netaddr));
			{
				unique_lock<mutex> lck(g_visit_mux);
				to_visit.push(message);
			}

		}
	}
	if (total_cnt) { 
		g_visit_cond.notify_all();
	}
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
