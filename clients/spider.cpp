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

/* This is a simple test client to demonstrate use of the connector */


using namespace std;
using namespace ctrl;

uint32_t g_msg_id; /* left in network byte order */


void watch_cxn(const string &);
void fetch_addrs(const string &);
void append_getaddr(uint32_t handle /* host byte order */, const struct sockaddr_in &remote);

struct pm_type { /* used to use a tuple. having the names is nice
                    though, but that's the reason for the names */
	wrapped_buffer<uint8_t> buf;
	size_t size;
	struct sockaddr_in remote; /* who this is on behalf of */
	time_t insertion_time;
	pm_type(wrapped_buffer<uint8_t> &a, size_t b, struct sockaddr_in c) :
		buf(a), size(b), remote(c), insertion_time(time(NULL)) {}
	pm_type(wrapped_buffer<uint8_t> &a, size_t b, struct sockaddr_in c, time_t d) :
		buf(a), size(b), remote(c), insertion_time(d) {}
};

struct pm_cmp {
	bool operator()(const struct pm_type &lhs, const struct pm_type &rhs) {
		return rhs.insertion_time < lhs.insertion_time;
	}
};

atomic_ulong g_outstanding_connects(0);
atomic_ulong g_successful_connects(0);

mutex g_getaddr_mux;
priority_queue<pm_type, vector<pm_type>, pm_cmp> pending_getaddr;

mutex g_connect_mux;
deque<struct pm_type> pending_connects;

struct addr_cmp {
	bool operator()(const struct sockaddr_in &lhs, const struct sockaddr_in &rhs) {
		int t = lhs.sin_addr.s_addr - rhs.sin_addr.s_addr;
		if (!t) {
			t = lhs.sin_port - rhs.sin_port;
		}
		return t < 0;
	}
};

struct metrics {
	time_t connect_time; /* updated each successful connect. If unknown set when discovered, 0 if not connected */
	time_t getaddr_time; /* when we last did a getaddr */
	metrics(time_t a, time_t b) : connect_time(a), getaddr_time(b) {}
	metrics() : metrics(0,0) {}
	metrics & operator=(const metrics &m) { 
		connect_time = m.connect_time;
		getaddr_time = m.getaddr_time; 
		return *this; 
	}
};

mutex g_map_mux;
map<struct sockaddr_in, struct metrics, addr_cmp> addr_map;



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

	{
		unique_lock<mutex> lck(g_map_mux);
		auto it = addr_map.find(remote);
		if (it != addr_map.end()) { /* already connected */
			return;
		}
	}

	{
		unique_lock<mutex> lck(g_connect_mux);
		wrapped_buffer<uint8_t> buf(sizeof(struct connect_message));
		struct connect_message *message = (struct connect_message*) buf.ptr();

		message->version = 0; 
		message->length = hton((uint32_t)sizeof(connect_payload));
		message->message_type = CONNECT;
		message->payload.local_addr.sin_family = AF_INET;
		message->payload.local_addr.sin_addr.s_addr = 0; /* current version of connector ignores this */
		message->payload.local_addr.sin_port = 0;

		memcpy(&message->payload.remote_addr, &remote, sizeof(remote));

		pending_connects.push_back(pm_type(buf, sizeof(*message), message->payload.remote_addr));
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
		addrs[0].sin_family = AF_INET;
		addrs[0].sin_port = htons(8333);
		bzero(&addrs[1], sizeof(addrs[1]));
	}

	for(const struct sockaddr_in *cur = addrs; cur->sin_family != 0; ++cur) {
		append_connect(*cur);
	}

	if (rootfile) {
		close(fd);
		munmap(addrs, mult * page_size);
	} else {
		delete [] addrs;
	}

}

void do_getaddrs(int sock, time_t now) {
	unique_lock<mutex> lock(g_getaddr_mux);
	/* do all pending_getaddrs */
	while(pending_getaddr.size() && (now - pending_getaddr.top().insertion_time) > 0) {
		auto topaddr(pending_getaddr.top());
		pending_getaddr.pop();
		lock.unlock();

		{
			unique_lock<mutex> maplock(g_map_mux);
			addr_map[topaddr.remote].getaddr_time = now;
		}
		do_write(sock, topaddr.buf.const_ptr(), topaddr.size);
		lock.lock();
	}
}

const size_t MAX_ONGOING_CONNECTS(10000);

void do_connects(int sock) {
	unique_lock<mutex> lock(g_connect_mux);
	while(g_outstanding_connects < MAX_ONGOING_CONNECTS && pending_connects.size()) {
		/* do all pending connects*/
		auto pending(pending_connects.front());
		pending_connects.pop_front();
		const struct message *msg = (const struct message*) pending.buf.const_ptr();

		lock.unlock();

		assert(msg->message_type == CONNECT);
		bool do_connect = false;
		const struct connect_message *con_msg  = (const struct connect_message*) msg;
		{
			unique_lock<mutex> map_lock(g_map_mux);
			auto it = addr_map.find(con_msg->payload.remote_addr);
			if (it == addr_map.end()) { /* don't know about him yet (i.e., not yet connected) */
				addr_map[con_msg->payload.remote_addr] = metrics(0,0);
				do_connect = true;
			} else if (it->second.connect_time == 0) {
				/* this is a case where we know about the person, but are not connected to them yet for whatever reason. We don't want to attempt to connect again here after all */

				//do_connect = true; 
			}
		}

		if (do_connect) {
			cout << "Connect attempt to " << *((struct sockaddr*) &con_msg->payload.remote_addr) << endl;
			assert(pending.size == sizeof(*con_msg));
			do_write(sock, con_msg, pending.size);
		}

		lock.lock();
		if (do_connect) { 
			++g_outstanding_connects;
		}



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

	register_getaddr(sock);
	append_roots(NULL);
	time_t now(time(NULL));
	get_all_cxn(sock, [&](struct connection_info *info, size_t ) {
			unique_lock<mutex> lck(g_map_mux);
			addr_map[info->remote_addr].connect_time = now;
			append_getaddr(ntoh(info->handle_id), info->remote_addr);
		});


	string root((const char*)cfg->lookup("logger.root"));
	mkdir(root.c_str(), 0777);
	string client_dir = root + "clients/";

	/* okay, start up worker threads */
	thread cxn_watcher(watch_cxn, client_dir);
	thread fetcher(fetch_addrs, client_dir);

	cxn_watcher.detach();
	fetcher.detach();


	/* this is a lazy spider. It assumes we have a local bitcoin guy to start
	   with and sets it as the start. It would be pretty easy to set
	   this up to pick these guys off of the DNS. Just being lazy so I
	   can watch the initiation */

	while(true) {

		now = time(NULL);

		/* TODO: fix busy wait on this thread */
		do_getaddrs(sock, now);
		do_connects(sock);

	}

	return EXIT_SUCCESS;
};


void handle_message(read_buffer &input_buf) {
	const uint8_t *buf = input_buf.extract_buffer().const_ptr();
	enum log_type lt(static_cast<log_type>(buf[0]));

	assert(lt == BITCOIN_MSG);
	//time_t time = ntoh(*( (uint64_t*)(buf+1)));
	
	const uint8_t *msg = buf + 8 + 1;

	//uint32_t id = ntoh(*((uint32_t*) msg));
	bool is_sender = *((bool*) (msg+4));

	const struct bitcoin::packed_message *bc_msg = (const struct bitcoin::packed_message*) (msg + 5);
	const struct bitcoin::full_packed_net_addr *addrs;
	if (!is_sender && strcmp(bc_msg->command, "addr") == 0) { /* okay, we actually care */
		uint8_t size;
		uint64_t count = bitcoin::get_varint(bc_msg->payload, &size);
		addrs = (const struct bitcoin::full_packed_net_addr*) (bc_msg->payload + size);

		for(size_t i = 0; i < count; ++i) {
			if (!is_private(addrs[i].rest.addr.ipv4.as.number)) {// && ntoh(addrs[i].rest.port) == 8333) {
				struct sockaddr_in netaddr = { AF_INET, 
				                               addrs[i].rest.port, 
				                               addrs[i].rest.addr.ipv4.as.in_addr, 
				                               {0}};
				append_connect(netaddr);
			}

		}
	}
}

void append_getaddr(uint32_t handle_id, const struct sockaddr_in &remote) {
	size_t total_len = sizeof(struct message) + sizeof(command_msg) + 4; /* room for one target */

	{
		unique_lock<mutex> lock(g_getaddr_mux);
		wrapped_buffer<uint8_t> buf(total_len);
		struct message *msg = (struct message *) buf.ptr();
		struct command_msg *cmsg = (struct command_msg*) ((uint8_t*) msg + sizeof(struct message));
		msg->version = 0;
		msg->length = hton((uint32_t)sizeof(*cmsg) + 4);
		msg->message_type = COMMAND;

		cmsg->command = COMMAND_SEND_MSG;
		cmsg->message_id = g_msg_id; /* already in NBO */
		cmsg->target_cnt = hton(1);
		cmsg->targets[0] = hton(handle_id);


		time_t now(time(NULL));
		for(size_t i = 0; i < 8; ++i) {
			pending_getaddr.push(pm_type(buf, total_len, remote, now + i * 10));
		}
	}
}


void watch_cxn(const string &client_dir) {
	int bitcoin_client = unix_sock_client(client_dir + "bitcoin", false);
	bcwatchers::bcwatch watcher(bitcoin_client, 
	                [](unique_ptr<bc_channel_msg> bc_msg) {
		                append_getaddr(bc_msg->handle_id, bc_msg->remote_addr);
		                cout << '(' << g_successful_connects << ") successful connect to " << *((struct sockaddr*) &bc_msg->remote_addr) << endl;
		                g_successful_connects += 1;

		                unsigned long expected, desired;
		                do {
			                expected = g_outstanding_connects.load();
			                desired = (expected == 0) ? 0 : expected - 1;
		                } while (!g_outstanding_connects.compare_exchange_weak(expected, desired));

		                {
			                unique_lock<mutex> lock(g_map_mux);
			                auto it = addr_map.find(bc_msg->remote_addr);
			                if (it != addr_map.end()) {
				                it->second.connect_time = time(NULL);
			                } else { /* we are learning about this for the first time */
				                addr_map[bc_msg->remote_addr] = { .connect_time = time(NULL), .getaddr_time = time(NULL) };
			                }
		                }
	                },
	                [](unique_ptr<bc_channel_msg> msg) {
		                g_outstanding_connects = min((size_t)0, g_outstanding_connects - 1);

		                unsigned long expected, desired;
		                do {
			                expected = g_outstanding_connects.load();
			                desired = (expected == 0) ? 0 : expected - 1;
		                } while (!g_outstanding_connects.compare_exchange_weak(expected, desired));

		                {
			                unique_lock<mutex> lock(g_map_mux);
			                auto it = addr_map.find(msg->remote_addr);
			                if (it != addr_map.end()) {
				                it->second.connect_time = 0;
			                } else { 
				                cerr << "Got a disconnect from someone you didn't know exists...\n";
			                }
		                }
		                if (0) {
			                cout << "Unsuccessful connect. " << endl;
			                cout << "\ttime: " << msg->header.timestamp << endl;
			                cout << "\tupdate_type: " << msg->update_type << endl;
			                cout << "\tremote: " << *((struct sockaddr*) &msg->remote_addr) << endl;
			                cout << "\ttext_length: " << msg->text_len << endl;
			                if (msg->text_len) {
				                cout << "\ttext: " << msg->text << endl;
			                }
		                }
	                });
	watcher.loop_forever();
}
	                


void fetch_addrs(const string &client_dir) {

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
