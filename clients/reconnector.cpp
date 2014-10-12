/* standard C libraries */
#include <cstdlib>
#include <cstring>
#include <cassert>

/* standard C++ libraries */
#include <iostream>
#include <utility>
#include <queue>
#include <unordered_map>
#include <random>

/* standard unix libraries */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

#include "lib.hpp"
#include "network.hpp"
#include "connector.hpp"
#include "bitcoin.hpp"
#include "config.hpp"
#include "logger.hpp"
#include "bcwatch.hpp"


using namespace std;
using namespace ctrl::easy;

struct in_addr g_local_addr;

random_device g_rdev;
mt19937 g_gen(g_rdev());


struct sockaddr_hash {
	size_t operator()(const struct sockaddr_in &a) const {
		return hash<uint32_t>()(a.sin_addr.s_addr) + hash<uint16_t>()(a.sin_port);
	}
};

struct sockaddr_keyeq {
	bool operator()(const struct sockaddr_in &a, const struct sockaddr_in &b) const {
		return a.sin_addr.s_addr == b.sin_addr.s_addr &&
			a.sin_port == b.sin_port;
	}
};



struct queue_entry {
	uint32_t retry_time;
	sockaddr_in addr;
	queue_entry(uint32_t rt_, const sockaddr_in &addr_) : retry_time(rt_), addr(addr_) {}
};

struct entry_cmp {
	bool operator()(const struct queue_entry &lhs, const struct queue_entry &rhs) {
		return rhs.retry_time < lhs.retry_time;
	}
};

priority_queue<queue_entry, vector<queue_entry>, entry_cmp> g_pending_connects;
struct unordered_map<sockaddr_in, int, sockaddr_hash, sockaddr_keyeq> g_fail_cnt;

int main(int argc, char *argv[]) {

	if (startup_setup(argc, argv) != 0) {
		return EXIT_FAILURE;
	}

	const libconfig::Config *cfg(get_config());

	libconfig::Setting &list = cfg->lookup("connector.bitcoin.listeners");
	if (list.getLength() == 0) {
		return EXIT_FAILURE;
	}

	libconfig::Setting &local_cxn = list[0];
	const char *local_arg = (const char *)local_cxn[1];
	local_arg = "the correct value is the address of your NIC";
	if (inet_aton(local_arg, &g_local_addr) == 0) {
		cerr << "bad aton on " << local_arg << endl;
		return EXIT_FAILURE;
	}

	int sock = unix_sock_client((const char*)cfg->lookup("connector.control_path"), false);

	string root((const char*)cfg->lookup("logger.root"));
	mkdir(root.c_str(), 0777);
	string client_dir(root + "clients/");
	int bitcoin_client = unix_sock_client(client_dir + "bitcoin", false);
	bcwatch watcher(bitcoin_client, 
	                [](unique_ptr<struct bc_channel_msg> msg) {
		                struct sockaddr_in remote_addr;
		                if (msg->remote.sin_addr.s_addr == g_local_addr.s_addr) {
			                /* they connected to us, so local is our remote */
			                memcpy(&remote_addr, &msg->local, sizeof(remote_addr));
		                } else if (msg->local.sin_addr.s_addr == g_local_addr.s_addr) {
			                memcpy(&remote_addr, &msg->remote, sizeof(remote_addr));
		                } else {
			                cout << msg->remote << " and " << msg->local << " matched no one\n";
			                return;
		                }
		                g_fail_cnt[remote_addr] = -1; /* we are connected, not failed */

	                },
	                [&](unique_ptr<struct bc_channel_msg> msg) {
		                if (msg->update_type & (ORDERLY_DISCONNECT| WRITE_DISCONNECT | PEER_RESET)) {

			                struct sockaddr_in remote_addr;
			                if (msg->remote.sin_addr.s_addr == g_local_addr.s_addr) {
				                /* they connected to us, so local is our remote */
				                memcpy(&remote_addr, &msg->local, sizeof(remote_addr));
			                } else if (msg->local.sin_addr.s_addr == g_local_addr.s_addr) {
				                memcpy(&remote_addr, &msg->remote, sizeof(remote_addr));
			                } else {
				                cout << msg->remote << " and " << msg->local << " matched no one\n";
				                return;
			                }

			                if (msg->handle_id) { /* we were connected, now we aren't */
				                g_pending_connects.emplace(time(NULL), remote_addr);
				                g_fail_cnt[remote_addr] = 0;
			                } else {
				                size_t cnt = ++g_fail_cnt[remote_addr];
				                /* let's just suppose are slots are 0, 5, 10, 15 ... */
				                if (cnt > 30) {
					                g_fail_cnt.erase(remote_addr); /* give up */
				                } else {
					                uniform_int_distribution<> dis(0, cnt);
					                uint8_t d = dis(g_gen);
					                uint32_t wait = 5 * ((1 << d) - 1);
					                cerr << "Waiting for " << remote_addr << " for " << wait << " seconds\n";
					                g_pending_connects.emplace(time(NULL) + wait, remote_addr);
				                }
			                }

		                }
	                });


	/* doesn't matter, connector doesn't pay attention to this right now */
	struct sockaddr_in local_addr;
	bzero(&local_addr, sizeof(local_addr));
	local_addr.sin_family = AF_INET;
	if (inet_pton(AF_INET, "127.0.0.1", &local_addr.sin_addr) != 1) {
		perror("inet_pton source");
		return EXIT_FAILURE;
	}
	local_addr.sin_port = hton(static_cast<uint16_t>(0xdead));

	for(;;) {
		time_t now = time(NULL);
		while(g_pending_connects.size() && now - g_pending_connects.top().retry_time > 0) {
			struct queue_entry qe(g_pending_connects.top());
			g_pending_connects.pop();
			if (g_fail_cnt[qe.addr] >= 0) {
				cout << "Reconnecting " << qe.addr << endl;
				connect_msg message(&qe.addr, &local_addr);
				pair<wrapped_buffer<uint8_t>, size_t> p = message.serialize();
				do_write(sock, p.first.ptr(), p.second);
			} else {
				/* This means we connected to them some time after putting them in the queue, so nothing to do */
			}

		}
		watcher.loop_once();
	}
	
	return EXIT_SUCCESS;
};
