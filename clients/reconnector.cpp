/* standard C libraries */
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cmath>

/* standard C++ libraries */
#include <iostream>
#include <utility>
#include <queue>
#include <unordered_map>
#include <random>
#include <mutex>
#include <condition_variable>
#include <thread>

/* standard unix libraries */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

/* third party libraries */
#include <boost/operators.hpp>

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

mutex g_pending_mux;
condition_variable g_pending_cv;
mutex g_connected_mux;

random_device g_rdev;
mt19937 g_gen(g_rdev());

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

#pragma GCC diagnostic ignored "-Weffc++"
/* this class gives a gross warning. It's because boost::operators
   doesn't have a default destructor. This isn't necessary because I
   don't know if you can instantiate it in any sensible way, so
   turning off that warning here. */
template<int E = 2, int L = 3600> /* with the defaults, L ends up being the half-life in seconds */
class decaying_count : boost::operators<decaying_count<E,L> > {
public:
	decaying_count() : count_(0), last_(time(NULL)) {}
	decaying_count(double initial) : count_(initial), last_(time(NULL)) {}
	decaying_count(double initial, time_t now) : count_(initial), last_(now) {}
	bool operator<(decaying_count<E,L> x) { decay(); x.decay(); return count_ < x.count_; }
	bool operator==(decaying_count<E,L> x) { decay(); x.decay(); return count_ == x.count_; }
	decaying_count<E,L>& operator+=(decaying_count<E,L> x) {
		decay(); x.decay();
		count_ += x.count_;
		return *this;
	}
	decaying_count<E,L>& operator-=(decaying_count<E,L> x) {
		decay(); x.decay();
		count_ -= x.count_;
		return *this;
	}
	decaying_count<E,L>& operator*=(decaying_count<E,L> x) {
		decay(); x.decay();
		count_ *= x.count_;
		return *this;
	}
	decaying_count<E,L>& operator/=(decaying_count<E,L> x) {
		decay(); x.decay();
		count_ /= x.count_;
		return *this;
	}
	decaying_count<E,L>& operator++() {
		decay();
		count_ += 1;
		return *this;
	}
	decaying_count<E,L>& operator--() {
		decay();
		count_ += 1;
		return *this;
	}
	operator double() { decay(); return count_; }
	operator int() { decay(); return int(count_); }
	operator size_t() { decay(); return size_t(count_); }
private:
	void decay() { 
		time_t now = time(NULL);
		time_t since = now - last_;
		count_ *= pow(E, -1.0/L * since);
		last_ = now;
	}
	double count_;
	time_t last_;
};
#pragma GCC diagnostic warning "-Weffc++"


priority_queue<queue_entry, vector<queue_entry>, entry_cmp> g_pending_connects;
unordered_map<sockaddr_in, decaying_count<>, sockaddr_hash, sockaddr_keyeq> g_attempt_cnt;
unordered_map<sockaddr_in, bool, sockaddr_hash, sockaddr_keyeq> g_connected;


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
	local_arg = "172.31.30.19";
	local_arg = "Set to local interface";
	if (inet_aton(local_arg, &g_local_addr) == 0) {
		cerr << "bad aton on " << local_arg << endl;
		return EXIT_FAILURE;
	}

	int sock = unix_sock_client((const char*)cfg->lookup("connector.control_path"), false);

	get_all_cxn(sock, [&](struct ctrl::connection_info *info, size_t ) {
			unique_lock<mutex> lck(g_connected_mux);
			g_connected[info->remote_addr] = true;
		});
	
	string root((const char*)cfg->lookup("logger.root"));
	mkdir(root.c_str(), 0777);
	string client_dir(root + "clients/");
	int bitcoin_client = unix_sock_client(client_dir + "bitcoin", false);

	bcwatchers::bcwatch watcher(bitcoin_client, 
	                [](unique_ptr<bc_channel_msg> msg) {
		                unique_lock<mutex> lck(g_connected_mux);
		                g_connected[msg->remote_addr] = true;
	                },
	                [&](unique_ptr<bc_channel_msg> msg) {
		                bool do_connect = false;
		                {
			                unique_lock<mutex> lck(g_connected_mux);
			                g_connected.erase(msg->remote_addr);
		                }

		                do_connect = msg->update_type & (CONNECT_FAILURE | ORDERLY_DISCONNECT| WRITE_DISCONNECT | PEER_RESET);
		                if (!do_connect && msg->update_type & (UNEXPECTED_ERROR)) {
			                do_connect = strcmp(msg->text, "Connection timed out") == 0;
		                }
		                if (do_connect) {
			                size_t cnt = ++g_attempt_cnt[msg->remote_addr];
			                /* let's just suppose are slots are 0, 5, 10, 15 ... */
			                if (cnt > 12) {
				                g_attempt_cnt.erase(msg->remote_addr); /* give up */
			                } else {
				                uniform_int_distribution<> dis(0, cnt);
				                uint8_t d = dis(g_gen);
				                uint32_t wait = 5 * ((1 << d) - 1);
				                cerr << "Waiting for " << msg->remote_addr << " for " << wait << " seconds\n";
				                struct sockaddr_in remote;
				                memcpy(&remote, &msg->remote_addr, sizeof(remote));
				                {
					                unique_lock<mutex> pending_lck(g_pending_mux);
					                g_pending_connects.emplace(time(NULL) + wait, remote);
				                }
				                g_pending_cv.notify_one();
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


	thread bc_thread([&]() { watcher.loop_forever(); });

	for(;;) {
		time_t now = time(NULL);
		/* SUCH NASTY HACKED IN CRAP MT. Stay classy me. */
		unique_lock<mutex> pending_lck(g_pending_mux);
		if (g_pending_connects.size() && now - g_pending_connects.top().retry_time > 0) {
			struct queue_entry qe(g_pending_connects.top());
			g_pending_connects.pop();
			pending_lck.unlock();
			unique_lock<mutex> connected_lck(g_connected_mux);
			if (!g_connected[qe.addr]) { 
				connected_lck.unlock();
				cout << "Reconnecting " << qe.addr << endl;
				connect_msg message(&qe.addr, &local_addr);
				pair<wrapped_buffer<uint8_t>, size_t> p = message.serialize();
				do_write(sock, p.first.ptr(), p.second);
			}
		} else if (g_pending_connects.size()) {
			time_t diff = min((time_t)1,now - g_pending_connects.top().retry_time);
			pending_lck.unlock();
			sleep(diff);
			pending_lck.lock();
		} else {
			g_pending_cv.wait(pending_lck, [&]{return g_pending_connects.size();});
		}
	}
	
	return EXIT_SUCCESS;
};
