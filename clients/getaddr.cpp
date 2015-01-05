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


using namespace std;
using namespace ctrl;

class cxn_handler;
class hid_handler;



int g_control; /* control socket */
vector<uint32_t> g_pending_getaddrs; /* hids to getaddr in the next "round" */
uint32_t g_msg_id; /* in host byte order */


/* we will store all addresses we ever see in this structure. The
   handle_id will be set to ~0 if we do not have an active connection
   for a given address */
map<struct sockaddr_in, uint32_t, sockaddr_cmp> g_known_addrs; /* sockaddr -> hid */

map<uint32_t, hid_handler> g_known_hids; /* hid -> handler */
map<struct sockaddr_in, cxn_handler> g_cxns;




/* ev handler for each hid. This is timer based. All it can do is send
   getaddrs and it can only have one queued at a time. Attempting to
   enqueue another one resets its queue time */
class hid_handler { 
public:
	hid_handler(uint32_t hid_, const struct sockaddr_in *remote_, const struct sockaddr_in *local_, bool enqueue_ = true)
		: hid(hid_), timer(), last_enqueue() {
		memcpy(&remote, remote_, sizeof(remote));
		memcpy(&local, local_, sizeof(local));
		timer.set<hid_handler, &hid_handler::timer_cb>(this);
		if (enqueue_) {
			enqueue();
		}
	}

	~hid_handler() {
		timer.stop();
	}

	void timer_cb(ev::timer &w, int revents) {
		ev::tstamp after = 10 + last_enqueue - ev::now(ev_default_loop());
		if (after <= 0.0) { /* waiting period expired */
			g_pending_getaddrs.push_back(hid);
		} else {
			timer.stop();
			timer.set(after);
			timer.start();
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

	hid_handler & operator=(hid_handler other);
	hid_handler(const hid_handler &);
	hid_handler(const hid_handler &&other);
	hid_handler & operator=(hid_handler &&other);
};

class bc_msg_handler {
public:
	bc_msg_handler(int fd) : read_queue(sizeof(uint32_t)), io(), reading_len(true) {
		io.set<bc_msg_handler, &bc_msg_handler::io_cb>(this);
		io.set(fd, ev::READ);
		io.start();
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
							uint8_t bits = 0;
							uint64_t entries = bitcoin::get_varint(blog->msg.payload, &bits);
							const struct bitcoin::full_packed_net_addr *addrs = (const struct bitcoin::full_packed_net_addr*) ((uint8_t*)blog->msg.payload + bits);
							struct sockaddr_in to_insert;
							bzero(&to_insert, sizeof(to_insert));

							auto it = g_known_hids.find(handle_id);
							if (it == g_known_hids.end()) {
								g_log<ERROR>("Got address from hid ", handle_id, " but could not find hid handler for it");
							} else {
								it->second.enqueue();
							}


							for(size_t i = 0; i < entries; ++i) {

								struct sockaddr_in to_insert;
								bzero(&to_insert, sizeof(to_insert));
								for(size_t i = 0; i < entries; ++i) {
									if (!is_private(addrs[i].rest.addr.ipv4.as.number)) {
										/* TODO: verify it is ipv4, since we don't support ipv6 */
										memcpy(&to_insert.sin_addr, &addrs[i].rest.addr.ipv4.as.in_addr, sizeof(to_insert.sin_addr));
										to_insert.sin_port = addrs[i].rest.port;
										to_insert.sin_family = AF_INET;

										auto it = g_known_addrs.find(to_insert);
										if (it == g_known_addrs.end()) {
											g_known_addrs.insert(make_pair(to_insert, (uint32_t)~0));
										
										}
									
									}
								}
							}

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

	void suicide() {
	}


	read_buffer read_queue;
	ev::io io;
	bool reading_len;
	bc_msg_handler & operator=(bc_msg_handler other);
	bc_msg_handler(const bc_msg_handler &);
	bc_msg_handler(const bc_msg_handler &&other);
	bc_msg_handler & operator=(bc_msg_handler &&other);
};


/* Essentially don't want to flap hard on some guy or
   pointlessly retry connects */

class cxn_handler { /* keep state for ongoing connection attempts/harvesting. Timer of next attempt, consecutive fails, whether a pending attempt is ongoing */
public:

	enum State { DISCONNECTED, CONNECTING, CONNECTED };
	cxn_handler(const struct sockaddr_in *remote_, State s = DISCONNECTED) : state_(s), pending_time(0), consecutive_fails(0), timer() {
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

	void timer_cb(ev::timer &w, int revents) {
		if (pending_time > 0) { /* we are still pending, so we either want to expire our pending flag or reset the timer to check it out later */
			ev::tstamp after = ev::now(ev_default_loop()) - (pending_time + 60*15);
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

void send_getaddrs(int fd, uint32_t *handle_ids, size_t handle_cnt) {
	if (handle_cnt == 0) {
		return;
	}

	ctrl::easy::command_msg msg(COMMAND_SEND_MSG, g_msg_id, handle_ids, handle_cnt);

	auto p = msg.serialize();
	do_write(fd, p.first.const_ptr(), p.second);
}

int main(int argc, char *argv[]) {

	if (startup_setup(argc, argv)) {
		return EXIT_FAILURE;
	}

	const libconfig::Config *cfg(get_config());

	string root((const char*)cfg->lookup("logger.root"));
	string client_dir = root + "clients/";

	int bc_msg_client = unix_sock_client(client_dir + "bitcoin_msg", false);
	int bc_client = unix_sock_client(client_dir + "bitcoin", false);
	g_control = unix_sock_client((const char*)cfg->lookup("connector.control_path"), false);


	register_getaddr(g_control);

	get_all_cxn(g_control, [&](struct connection_info *info, size_t ) {
			uint32_t hid = ntoh(info->handle_id);
			g_known_addrs.insert(make_pair(info->remote_addr, hid));
			/* TODO: handle case where remote is incoming instead of outgoing */
			g_cxns.insert(make_pair(info->remote_addr, cxn_handler(&info->remote_addr, cxn_handler::State::CONNECTED)));
			g_known_hids.insert(make_pair(hid, hid_handler(hid, &info->remote_addr, &info->local_addr, false)));
		});

	bcwatchers::ev_handler watcher(bc_client,
	                               [&](unique_ptr<bc_channel_msg> bc_msg) {
		                               /* TODO: disconnect old connection if it somehow exists and log this */
		                               /* TODO: handle case where remote is incoming instead of outgoing */
		                               g_known_addrs[bc_msg->remote_addr] = bc_msg->handle_id;
		                               auto it = g_cxns.find(bc_msg->remote_addr);
		                               if (it == g_cxns.end()) {
			                               g_cxns[bc_msg->remote_addr] = cxn_handler(&bc_msg->remote_addr, cxn_handler::State::CONNECTED);
		                               } else {
			                               it->second.state(cxn_handler::State::CONNECTED);
		                               }
		                               g_known_hids[bc_msg->handle_id] = hid_handler(bc_msg->handle_id, &bc_msg->remote_addr, &bc_msg->local_addr, false);
	                               },
	                               [&](unique_ptr<bc_channel_msg> bc_msg) {
		                               g_known_addrs[bc_msg->remote_addr] = -1;
		                               auto it = g_cxns.find(bc_msg->remote_addr);
		                               if (it == g_cxns.end()) {
			                               g_cxns.insert(make_pair(bc_msg->remote_addr, cxn_handler(&bc_msg->remote_addr, cxn_handler::State::DISCONNECTED)));
		                               } else {
			                               it->state(cxn_handler::State::DISCONNECTED);
		                               }
		                               g_known_hids.erase(bc_msg->handle_id);

	                               });

	bc_msg_handler addr_handler(bc_msg_client);

	signal(SIGPIPE, SIG_IGN);

	ev::default_loop loop;

	while(true) {
		cout << "Restarting loop\n";
		loop.run();
	}


	return EXIT_SUCCESS;
}

