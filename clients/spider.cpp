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
#include "read_buffer.hpp"
#include "bcwatch.hpp"
#include "lib.hpp"

/* This is a simple test client to demonstrate use of the connector */


using namespace std;
using namespace ctrl;

size_t g_successful_connects = 0;

uint32_t g_msg_id; /* left in network byte order */

mutex g_visit_mux;
condition_variable g_visit_cond;

void watch_cxn(const libconfig::Config *cfg);
void fetch_addrs(const libconfig::Config *cfg);

struct pm_type { /* used to use a tuple. having the names is nice
                    though, but that's the reason for the names */
	wrapped_buffer<uint8_t> buf;
	size_t size;
	struct sockaddr_in remote; /* who this is on behalf of */
	pm_type(wrapped_buffer<uint8_t> &a, size_t b, struct sockaddr_in c) :
		buf(a), size(b), remote(c) {}
	wrapped_buffer<uint8_t> first() { return buf; }
	size_t second() { return size; }
	struct sockaddr_in third() { return remote; }
};
deque<struct pm_type> pending_messages;

struct pvp_comp {
	bool operator()(const struct sockaddr_in &lhs, const struct sockaddr_in &rhs) {
		int t = lhs.sin_addr.s_addr - rhs.sin_addr.s_addr;
		if (!t) {
			t = lhs.sin_port - rhs.sin_port;
		}
		return t < 0;
	}
};

struct metrics {
	time_t connect_time; /* updated each successful connect, 0 if not connected */
	time_t discovery_time; /* when THIS SPIDER found out about it */
	time_t getaddr_time; /* when we last did a getaddr */
};

mutex g_addr_mux;
map<struct sockaddr_in, struct metrics, pvp_comp> addr_map;



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

void append_connect(const struct sockaddr_in &remote) {
	wrapped_buffer<uint8_t> buf(sizeof(struct connect_message));
	struct connect_message *message = (struct connect_message*) buf.ptr(); /* re-trigger COW check */

	message->version = 0; 
	message->length = hton((uint32_t)sizeof(connect_payload));
	message->message_type = CONNECT;
	message->payload.local_addr.sin_family = AF_INET;
	message->payload.local_addr.sin_addr.s_addr = 0; /* current version of connector ignores this */
	message->payload.local_addr.sin_port = 0;

	memcpy(&message->payload.remote_addr, &remote, sizeof(remote));
	{
		unique_lock<mutex> lck(g_visit_mux);
		pending_messages.push_back(pm_type(buf, sizeof(*message), message->payload.remote_addr));
	}
}

void append_roots(const char *rootfile) {
	struct sockaddr_in *addrs;
	int fd(-1);
	size_t mult = 0;
	size_t page_size = sysconf(_SC_PAGE_SIZE);
	/* if root file is non-null, assume it is just a file serialization of
	   a struct sockaddr_in array with an all zero entry (family == 0)
	   to mark the end */
	if (rootfile)  {
		fd = open(rootfile, O_RDONLY);
		if (fd < 0) {
			throw runtime_error(string("bad file") + strerror(errno));
		}

		struct stat statbuf;
		if (stat(rootfile, &statbuf) != 0) {
			throw runtime_error(string("bad stat") + strerror(errno));
		}

		size_t size = statbuf.st_size;
		while(++mult * page_size < size);
		addrs = (struct sockaddr_in*)mmap(NULL, mult * page_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (addrs == MAP_FAILED) {
			throw runtime_error(string("bad mmap") + strerror(errno));
		}
	} else {
		addrs = new sockaddr_in[2];
		if (inet_pton(AF_INET, "127.0.0.1", &addrs[0].sin_addr) != 1) {
			throw runtime_error(string("inet_pton error") + strerror(errno));
		}
		addrs[0].sin_port = 8333;
		bzero(&addrs[1], sizeof(addrs[1]));
	}

	for(const struct sockaddr_in *cur = addrs; cur->sin_family != 0; ++cur) {
		cout << "appending " << *((struct sockaddr*)cur) << endl;
		append_connect(*cur);
	}

	if (rootfile) {
		close(fd);
		munmap(addrs, mult * page_size);
	} else {
		delete [] addrs;
	}

}

int main(int argc, char *argv[]) {

	if (argc == 2) {
		load_config(argv[1]);
	} else {
		load_config("../netmine.cfg");
	}

	const libconfig::Config *cfg(get_config());

	int sock = unix_sock_client((const char*)cfg->lookup("connector.control_path"), false);

	append_roots("/tmp/nodelist-C0iOzq");

	return 0;

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

			bool requeue = false;

			lock.unlock();

			const struct message *msg = (const struct message*) pmf.buf.const_ptr();
			if (msg->message_type == CONNECT) {
				bool do_connect = false;
				const struct connect_message *con_msg  = (const struct connect_message*) msg;
				{
					unique_lock<mutex>(g_addr_mux);
					auto it = addr_map.find(con_msg->payload.remote_addr);
					if (it == addr_map.end()) { /* don't know about him yet (i.e., not yet connected) */
						addr_map[con_msg->payload.remote_addr] = { .connect_time = 0, .discovery_time = time(NULL), .getaddr_time = 0 };
						do_connect = true;
					} else if (it->second.connect_time == 0) {
						do_connect = true;
					}
				}

				if (do_connect) {
					cout << "Connect attempt to " << *((struct sockaddr*) &con_msg->payload.remote_addr) << endl;
					assert(pmf.size == sizeof(*con_msg));
					do_write(sock, con_msg, pmf.size);
				}

			} else {
				/* some other kind of message. Right now just getaddr. Change this if you add others */
				time_t now = time(NULL);
				if (now - addr_map[pmf.remote].getaddr_time < 10) { /* let's give 10 seconds between getaddrs */
					requeue = true;
				} else {
					addr_map[pmf.remote].getaddr_time = now;
					do_write(sock, msg, pmf.size);
				}
			}

			lock.lock();

			if (requeue) {
				pending_messages.push_back(pmf);
			}
		}
	}

	return EXIT_SUCCESS;
};

bool is_private(uint32_t ip) {
	/* endian assumptions live here */
	return
		(0x000000FF & ip) == 10  ||
		(0x0000FFFF & ip) == (192 | (168 << 8)) ||
		(0x0000F0FF & ip) == (172 | (16 << 8));
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

		for(size_t i = 0; i < count; ++i) {
			if (!is_private(addrs[i].rest.addr.ipv4.as.number) && ntoh(addrs[i].rest.port) == 8333) {
				struct sockaddr_in netaddr = { AF_INET, 
				                               addrs[i].rest.port, 
				                               addrs[i].rest.addr.ipv4.as.in_addr, 
				                               {0}};
				append_connect(netaddr);
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

		                {
			                unique_lock<mutex> lock(g_addr_mux);
			                auto it = addr_map.find(bc_msg->remote);
			                if (it != addr_map.end()) {
				                it->second.connect_time = time(NULL);
			                } else { /* we are learning about this for the first time */
				                addr_map[bc_msg->remote] = { .connect_time = time(NULL), .discovery_time = time(NULL), .getaddr_time = time(NULL) };
			                }
		                }

		                struct pm_type p(buf, total_len, bc_msg->remote);
		                {
			                unique_lock<mutex> lock(g_visit_mux);
			                for(size_t i = 0; i < 8; ++i) {
				                pending_messages.push_back(p);
			                }
		                }
		                g_visit_cond.notify_all();
	                },
	                [](struct bc_channel_msg *msg) {

		                {
			                unique_lock<mutex> lock(g_addr_mux);
			                auto it = addr_map.find(msg->remote);
			                if (it != addr_map.end()) {
				                it->second.connect_time = 0;
			                } else { 
				                cerr << "Got a disconnect from someone you didn't know exists...\n";
			                }
		                }

		                cout << "Unsuccessful connect. " << endl;
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
