/* standard C libraries */
#include <cstdlib>
#include <cstring>
#include <cassert>

/* standard C++ libraries */
#include <iostream>
#include <utility>
#include <unordered_map>
#include <map>
#include <stack>
#include <memory>
#include <set>
#include <queue>
#include <random>

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
#include "connector.hpp"
#include "bitcoin.hpp"
#include "config.hpp"
#include "logger.hpp"
#include "read_buffer.hpp"
#include "lib.hpp"
#include "bcwatch.hpp"
#include "netwrap.hpp"

#define DO_CRON
#define HARVEST_CXN

#define GETADDR_LIMIT 24


using namespace std;
using namespace ctrl;

class cxn_handler;
class hid_handler;


uint32_t g_current_getaddr(1); /* identifier for current running getaddr */

ev_tstamp g_getaddr_start = 0;

int g_control; /* control socket */
vector<uint32_t> g_pending_getaddrs; /* hids to getaddr in the next "round" */
uint32_t g_msg_id; /* in host byte order */


/* we will store all addresses we ever see in this structure. The
   handle_id will be set to ~0 if we do not have an active connection
   for a given address */
unordered_map<struct sockaddr_in, uint32_t, sockaddr_hash, sockaddr_keyeq> g_known_addrs; /* sockaddr -> hid */

map<uint32_t, unique_ptr<hid_handler> > g_known_hids; /* hid -> handler */
unordered_map<struct sockaddr_in, unique_ptr<cxn_handler>, sockaddr_hash, sockaddr_keyeq > g_cxns;


inline bool do_sample() {
	static mt19937 twister;
	static float rate(0.0);
	bool x;
	if (rate == 0) {
		const libconfig::Config *cfg(get_config());
		rate = cfg->lookup("getaddr.sampling_rate");
	}

	if (g_known_addrs.size() < 1000) {
		x = true;
	} else {
		bernoulli_distribution d(rate);
		x = d(twister);
	}
	return x;
}

void register_getaddr(int sock) {
	/* register getaddr message */
	unique_ptr<struct bitcoin::packed_message> getaddr(bitcoin::get_message("getaddr"));
	ctrl::easy::bitcoin_msg msg((uint8_t*)getaddr.get(), sizeof(bitcoin::packed_message) + getaddr->length);

	auto p = msg.serialize();
	do_write(g_control, p.first.const_ptr(), p.second);

	/* response is to send back a uint32_t in NBO and that's all */
	if (recv(sock, &g_msg_id, sizeof(g_msg_id), MSG_WAITALL) != sizeof(g_msg_id)) {
		perror("registration read");
		abort();
	}
	g_msg_id = ntoh(g_msg_id);
}

void send_getaddrs(const vector<uint32_t> &handle_ids) {
	if (handle_ids.size() == 0) {
		return;
	}

	ctrl::easy::command_msg msg(COMMAND_SEND_MSG, g_msg_id, handle_ids);

	auto p = msg.serialize();
	do_write(g_control, p.first.const_ptr(), p.second);
}


/* just sends all pending getaddrs on a periodic basis. It's more
   efficient to batch these a bit */
class getaddr_pusher {
public:
	getaddr_pusher() : timer() {
		timer.set<getaddr_pusher, &getaddr_pusher::timer_cb>(this);
		timer.set(2, 2);
		timer.start();
	}

	~getaddr_pusher() { timer.stop(); }
	void timer_cb(ev::timer &, int ) {
		send_getaddrs(g_pending_getaddrs);
		g_pending_getaddrs.clear();
	}
private:
	ev::timer timer;
};



/* ev handler for each hid. This is timer based. All it can do is send
   getaddrs and it can only have one queued at a time. Attempting to
   enqueue another one resets its queue time */
class hid_handler { 
public:
	hid_handler(uint32_t hid_, const struct sockaddr_in *remote_, const struct sockaddr_in *local_)
		: hid(hid_), remote(), local(), timer(), last_enqueue(), getaddr_seq(g_current_getaddr), getaddr_cnt(0) {
		memcpy(&remote, remote_, sizeof(remote));
		memcpy(&local, local_, sizeof(local));
		timer.set<hid_handler, &hid_handler::timer_cb>(this);

		/* any new connections within 10 minutes should be part of the experiment */
		if ( (ev::now(ev_default_loop()) - g_getaddr_start) < 600) { 
			enqueue();
		}
	}

	~hid_handler() {
		timer.stop();
	}

	void timer_cb(ev::timer &, int) {
		ev::tstamp after = 10 + last_enqueue - ev::now(ev_default_loop());
		if (after <= 0.0) { /* waiting period expired */
			if (
#ifdef DO_CRON
			    getaddr_seq == g_current_getaddr &&
#endif
			    getaddr_cnt < GETADDR_LIMIT) { /* if we are no longer in an active getaddr, skip it */
				++getaddr_cnt;
				g_pending_getaddrs.push_back(hid);

			}
		} else {
			timer.stop();
			timer.set(after);
			timer.start();
		}
	}

	void set_sequence(uint32_t s) {
		if (s != getaddr_seq) {
			getaddr_seq = s;
			getaddr_cnt = 0; /* reset count if this is a new getaddr sequence */
		}
	}

	void enqueue() {
		/* TODO: add a stop on the number of getaddrs sent in a 24 hour period */
		last_enqueue = ev::now(ev_default_loop());
		if (!timer.is_active()) {
			timer.set(10);
			timer.start();
		}
	}

	const struct sockaddr_in * get_remote() const {
		return &remote;
	}

	const struct sockaddr_in * get_local() const {
		return &local;
		
	}


private:
	uint32_t hid;
	struct sockaddr_in remote;
	struct sockaddr_in local;
	ev::timer timer;
	ev::tstamp last_enqueue;
	uint32_t getaddr_seq;
	uint32_t getaddr_cnt;

	hid_handler & operator=(hid_handler other);
	hid_handler(const hid_handler &);
	hid_handler(const hid_handler &&other);
	hid_handler & operator=(hid_handler &&other);
};



/* Essentially don't want to flap hard on some guy or
   pointlessly retry connects */

class cxn_handler { /* keep state for ongoing connection attempts/harvesting. Timer of next attempt, consecutive fails, whether a pending attempt is ongoing */
public:

	enum State { DISCONNECTED, CONNECTING, CONNECTED };
	cxn_handler(const struct sockaddr_in *remote_, State s = DISCONNECTED) : state_(s), pending_time(0), consecutive_fails(0), sers(), timer() {
		static sockaddr_in local_addr; /* TODO: if this ever matters, fix it. curse of the API */
		ctrl::easy::connect_msg msg(remote_, &local_addr);
		sers = msg.serialize();
		timer.set<cxn_handler, &cxn_handler::timer_cb>(this);
		timer_cb(timer, 0);
	}

	State state(State s) {
		State old = state_;
		state_ = s;
		if (state_ == DISCONNECTED && !timer.is_active()) {
			set_reconnect_timer();
		}
		pending_time = 0;
		return old;
	}

	State state() {
		return state_;
	}


	~cxn_handler() {
		timer.stop();
	}
private:

	void timer_cb(ev::timer &, int ) {
		if (pending_time > 0) { /* we are still pending, so we either want to expire our pending flag or reset the timer to check it out later */
			ev::tstamp after = (pending_time + 60*15) - ev::now(ev_default_loop());
			if (after < 0.0) { /* for some reason, which is either a bug or the connector died or the logserver died...we didn't get the connect failed message and it's been 15 minutes. Just clear it */
				pending_time = 0;
				if (state_ == CONNECTING) {
					state_ = DISCONNECTED;
				}
			} else {
				timer.set(after);
				timer.start();
			}
			
		}
		if (pending_time == 0) {
			switch(state_) {
			case CONNECTING:
				/* We'll be back. Intentionally left blank */
				assert(timer.is_active());
				break;
			case CONNECTED:
				if (consecutive_fails) { /* slowly unwind the fails so if someone flaps they don't escape our backoff */
					--consecutive_fails;
					timer.set(10);
					timer.start();
				}
				break;
			case DISCONNECTED:
				state_ = CONNECTING;
				do_write(g_control, sers.first.const_ptr(), sers.second);
				pending_time = ev::now(ev_default_loop());
				consecutive_fails = min((size_t)14, consecutive_fails+1);;
				set_reconnect_timer();
				break;

			}
		}
	}

	void set_reconnect_timer() {
		uniform_int_distribution<uint32_t> uid(0, consecutive_fails);
		ev::tstamp when = 5 * ((1 << uid(twister)) - 1);
		timer.set(when);
		timer.start();

	}



	cxn_handler::State state_;
	ev::tstamp pending_time;
	size_t consecutive_fails;
	pair<wrapped_buffer<uint8_t>, size_t> sers;
	ev::timer timer;


	static mt19937 twister;

	cxn_handler & operator=(cxn_handler other);
	cxn_handler(const cxn_handler &);
	cxn_handler(const cxn_handler &&other);
	cxn_handler & operator=(cxn_handler &&other);
	

};

mt19937 cxn_handler::twister;


class bc_msg_handler {
public:
	bc_msg_handler(int fd) : read_queue(sizeof(uint32_t)), io(), reading_len(true) {
		io.set<bc_msg_handler, &bc_msg_handler::io_cb>(this);
		io.set(fd, ev::READ);
		io.start();
	}

	~bc_msg_handler() {
		if (io.fd >= 0) {
			close(io.fd);
		}
		io.stop();
		io.fd = -1;
	}

	void io_cb(ev::io &watcher, int revents) {

		ssize_t r(1);
		if (revents & ev::READ) {
			while(r > 0 && read_queue.hungry()) {

				pair<int,bool> res(read_queue.do_read(watcher.fd));
				r = res.first;
				if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN) { 
					string e(strerror(errno));
					g_log<ERROR>(e, "(getaddr)");
					throw runtime_error(e);
				}

				if (r == 0) { /* got disconnected! */
					/* LOG disconnect */
					g_log<ERROR>("(getaddr)");
					throw runtime_error("Lost connection to bc_msg handler");
				}

				if (read_queue.to_read() == 0) {
					if (reading_len) {
						uint32_t netlen = *((const uint32_t*) read_queue.extract_buffer().const_ptr());
						read_queue.cursor(0);
						read_queue.to_read(ntoh(netlen));
						reading_len = false;
					} else {

						const struct bitcoin_msg_log_format *blog = (const struct bitcoin_msg_log_format*)read_queue.extract_buffer().const_ptr();
						if (! blog->is_sender && strcmp(blog->msg.command, "addr") == 0) {
							uint32_t handle_id = ntoh(blog->id);
							struct sockaddr_in to_insert;
							bzero(&to_insert, sizeof(to_insert));

							auto it = g_known_hids.find(handle_id);
							if (it == g_known_hids.end()) {
								g_log<ERROR>("Got address from hid ", handle_id, " but could not find hid handler for it");
							} else {
								it->second->enqueue();
							}


#ifdef HARVEST_CXN

							uint8_t bits = 0;
							uint64_t entries = bitcoin::get_varint(blog->msg.payload, &bits);
							const struct bitcoin::full_packed_net_addr *addrs = (const struct bitcoin::full_packed_net_addr*) ((uint8_t*)blog->msg.payload + bits);

							for(size_t i = 0; i < entries; ++i) {

								struct sockaddr_in to_insert;
								bzero(&to_insert, sizeof(to_insert));
								for(size_t i = 0; i < entries; ++i) {
									if (!is_private(addrs[i].rest.addr.ipv4.as.number) && do_sample()) {
										/* TODO: verify it is ipv4, since we don't support ipv6 */
										memcpy(&to_insert.sin_addr, &addrs[i].rest.addr.ipv4.as.in_addr, sizeof(to_insert.sin_addr));
										to_insert.sin_port = addrs[i].rest.port;
										to_insert.sin_family = AF_INET;

										auto it = g_known_addrs.find(to_insert); /* hot spot, ameliorated with sampling */
										if (it == g_known_addrs.end()) {
											g_known_addrs.insert(make_pair(to_insert, ~0));
											unique_ptr<cxn_handler> handler(new cxn_handler(&to_insert, cxn_handler::State::DISCONNECTED));
											g_cxns.insert(make_pair(to_insert, move(handler)));
										}
									
									}
								}
							}
#endif

						}
						read_queue.cursor(0);
						read_queue.to_read(4);
						reading_len = true;

					}
					
				}


			}
		}
	}
private:

	read_buffer read_queue;
	ev::io io;
	bool reading_len;
	bc_msg_handler & operator=(bc_msg_handler other);
	bc_msg_handler(const bc_msg_handler &);
	bc_msg_handler(const bc_msg_handler &&other);
	bc_msg_handler & operator=(bc_msg_handler &&other);
};


#ifdef DO_CRON

set<struct sockaddr_in, sockaddr_cmp> g_bound_addrs;


void clock_cb (struct ev_loop *, ev_periodic *, int) {
	++g_current_getaddr;
	g_getaddr_start = ev::now(ev_default_loop());

	g_log<CLIENT>("Initiating GETADDR probe");
	for(auto &p : g_known_hids) {
		p.second->set_sequence(g_current_getaddr);
	}


	vector<uint32_t> hids;

	for(auto &h: g_known_hids) {

		/* infer if inbound */		
		const struct sockaddr_in *local = h.second->get_local();
		if (g_bound_addrs.find(*local) == g_bound_addrs.end()) { /* not on a bound address, so must be an ephemeral port, i.e., outbound connection */
			hids.push_back(h.first);
		} 
	}

	if (hids.size()) {
		ctrl::easy::command_msg msg(COMMAND_DISCONNECT, 0, hids);
		auto p = msg.serialize();
		do_write(g_control, p.first.const_ptr(), p.second);
	}

}

time_t next_getaddr(time_t now) {
	time_t next(0);
	vector<int> hours;
	vector<int> minutes;
	const libconfig::Config *cfg(get_config());

	libconfig::Setting &hoursList = cfg->lookup("getaddr.schedule.hours");
	for(int index = 0; index < hoursList.getLength(); ++index) {
		hours.push_back(hoursList[index]);
	}

	libconfig::Setting &minutesList = cfg->lookup("getaddr.schedule.minutes");
	for(int index = 0; index < minutesList.getLength(); ++index) {
		minutes.push_back(minutesList[index]);
	}

	for(time_t base = time(NULL); next == 0; base += 86400) {
		struct tm timeinfo = *localtime(&base);
		for(auto h : hours) {
			timeinfo.tm_hour = h;
			for (auto m : minutes) {
				timeinfo.tm_min = m;
				time_t when = mktime(&timeinfo);
				if (next == 0 && when - now > 0) {
					next = when;
					return next;
				}
			}
		}
	}

	return next;

}

ev_tstamp next_getaddr(struct ev_periodic *, ev_tstamp now) {

	time_t rv = next_getaddr((time_t)now);
	struct tm timeinfo = *localtime(&rv);
	cout << "Scheduling next event for " << asctime(&timeinfo);
	return rv;
}




#endif


int main(int argc, char *argv[]) {

	if (startup_setup(argc, argv)) {
		return EXIT_FAILURE;
	}

	const libconfig::Config *cfg(get_config());

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
		g_bound_addrs.insert(bound_addr);
	}




	string root((const char*)cfg->lookup("logger.root"));
	string client_dir = root + "clients/";

	string logpath = root + "servers";
	try {
		g_log_buffer = new log_buffer(unix_sock_client(logpath, true));
	} catch (const network_error &e) {
		cerr << "WARNING: Could not connect to log server! " << e.what() << endl;
	}


	
	int bc_msg_client = unix_sock_client(client_dir + "bitcoin_msg", true);
	int bc_client = unix_sock_client(client_dir + "bitcoin", true);
	g_control = unix_sock_client((const char*)cfg->lookup("connector.control_path"), false);


	register_getaddr(g_control);

	get_all_cxn(g_control, [&](struct connection_info *info, size_t ) {
			uint32_t hid = ntoh(info->handle_id);
			struct sockaddr_in remote;
			memcpy(&remote, &info->remote_addr, sizeof(remote)); /* due to packing */
			g_known_addrs.insert(make_pair(remote, hid));
			/* TODO: handle case where remote is incoming instead of outgoing */
			unique_ptr<cxn_handler> handler(new cxn_handler(&info->remote_addr, cxn_handler::State::CONNECTED));
			g_cxns.insert(make_pair(remote, move(handler)));
			g_known_hids.insert(make_pair(hid, unique_ptr<hid_handler>(new hid_handler(hid, &info->remote_addr, &info->local_addr))));
		});

	bcwatchers::ev_handler
		watcher(bc_client,
		        [&](unique_ptr<bc_channel_msg> bc_msg) {
			        /* TODO: disconnect old connection if it somehow exists and log this */
			        /* TODO: handle case where remote is incoming instead of outgoing */
			        struct sockaddr_in remote;
			        memcpy(&remote, &bc_msg->remote_addr, sizeof(remote));
			        uint32_t hid = bc_msg->handle_id;

			        g_known_addrs[remote] = hid;
			        auto it = g_cxns.find(remote);
			        if (it == g_cxns.end()) {
				        unique_ptr<cxn_handler> handler(new cxn_handler(&remote, cxn_handler::State::CONNECTED));
				        g_cxns.insert(make_pair(remote, move(handler)));
			        } else {
				        it->second->state(cxn_handler::State::CONNECTED);
			        }
			        if (g_known_hids.erase(hid) > 0) {
				        g_log<DEBUG>("Received a hid more than once? ", hid);
			        }
			        unique_ptr<hid_handler> handler(new hid_handler(hid, &remote, &bc_msg->local_addr));
			        g_known_hids.insert(make_pair(hid, move(handler)));
		        },
		        [&](unique_ptr<bc_channel_msg> bc_msg) {
			        uint32_t hid = bc_msg->handle_id;
			        struct sockaddr_in remote;
			        memcpy(&remote, &bc_msg->remote_addr, sizeof(remote));
			        g_known_addrs[remote] = ~0;
			        auto it = g_cxns.find(remote);
			        if (it == g_cxns.end()) {
				        g_log<DEBUG>("Somehow we got a disconnect for a connection we did not know about to ", remote);
				        unique_ptr<cxn_handler> handler(new cxn_handler(&remote, cxn_handler::State::DISCONNECTED));
				        g_cxns.insert(make_pair(remote, move(handler)));
			        } else {
				        it->second->state(cxn_handler::State::DISCONNECTED);
			        }
			        g_known_hids.erase(hid);

		        },
		        [](const bcwatchers::ev_handler *) {}
		        );

	signal(SIGPIPE, SIG_IGN);

	ev::default_loop loop;
	bc_msg_handler addr_handler(bc_msg_client);
	getaddr_pusher pusher;

#ifdef DO_CRON

#pragma GCC diagnostic ignored "-Wstrict-aliasing"
		ev_periodic timer;
		ev_periodic_init(&timer, clock_cb, 0, 0, next_getaddr);
		ev_periodic_start(ev_default_loop(0), &timer);
#pragma GCC diagnostic warning "-Wstrict-aliasing"

#else

		g_log<CLIENT>("Initiating getaddr");
		g_pending_getaddrs.push_back(ctrl::BROADCAST_TARGET);

#endif


	while(true) {
		cout << "Starting loop\n";
		loop.run();
	}


	return EXIT_SUCCESS;
}

