#ifndef BITCOIN_HANDLER_HPP
#define BITCOIN_HANDLER_HPP

#include <cstdint>

#include <unordered_set>
#include <string>
#include <map>
#include <memory>

#include <netinet/in.h>

#include <ev++.h>

#include "read_buffer.hpp"
#include "write_buffer.hpp"

namespace bitcoin {


const uint32_t RECV_MASK = 0x0000ffff; // all receive flags should be in the mask 
const uint32_t RECV_HEADER = 0x1;
const uint32_t RECV_PAYLOAD = 0x2;
const uint32_t RECV_VERSION_INIT = 0x4; /* we initiated the handshake, now waiting to receive version */
const uint32_t RECV_VERSION_INIT_HDR = 0x8; /* we initiated the handshake, now waiting to receive version */
const uint32_t RECV_VERSION_REPLY_HDR = 0x10;
const uint32_t RECV_VERSION_REPLY = 0x20;

const uint32_t SEND_MASK = 0xffff0000;
const uint32_t SEND_MESSAGE = 0x10000;
const uint32_t SEND_VERSION_INIT = 0x20000; /* we initiated the handshake */
const uint32_t SEND_VERSION_REPLY = 0x40000;
   

class handler {
private:
	read_buffer read_queue; /* application needs to read and act on this data */

	write_buffer write_queue; /* application wants this written out across network */

	struct sockaddr_in remote_addr;

	struct sockaddr_in local_addr; /* address we connected on (TODO) */

	uint32_t timestamp;

	uint32_t state;

	int io_events;
	ev::io io;
	ev::timer timer; ev::tstamp last_activity;
	ev::timer active_ping_timer;
	uint32_t id;
	static uint32_t id_pool;

	inline void io_set(int e) {
		if (e != io_events) {
			io_events = e;
			io.set(io_events);
		}
	}

public:
	handler(int fd, uint32_t a_state, const struct sockaddr_in &a_remote_addr, const struct sockaddr_in &a_local_addr);
	~handler();
	uint32_t get_id() const { return id; }
	void handle_message_recv(const struct packed_message *msg);
	void io_cb(ev::io &watcher, int revents);
	void pinger_cb(ev::timer &w, int revents);
	void active_pinger_cb(ev::timer &w, int revents);
	struct sockaddr_in get_remote_addr() const { return remote_addr; }
	struct sockaddr_in get_local_addr() const { return local_addr; }
	/* appends message, leaves write queue unseeked, but increments to_write. */
	void append_for_write(const struct packed_message *m);
	void append_for_write(std::unique_ptr<struct packed_message> m);
	/* this is an optimized call for reducing copies. buf better be a packed_message internally */
	void append_for_write(wrapped_buffer<uint8_t> buf); 
	void disconnect();
private:
	void suicide(); /* get yourself ready for suspension (e.g., stop loop activity) if safe, just delete self */
	/* could implement move operators, but others are odd */
	handler & operator=(handler other);
	handler(const handler &);
	handler(const handler &&other);
	handler & operator=(handler &&other);


	void start_pingers();
	void do_read(ev::io &watcher, int revents);
	void do_write(ev::io &watcher, int revents);
};

struct handler_hashfunc {
	size_t operator()(const std::unique_ptr<handler> &h) const {
		return std::hash<uint32_t>()(h->get_id());
	}
};

struct handler_equal {
	bool operator()(const std::unique_ptr<handler> &lhs, const std::unique_ptr<handler> &rhs) const {
		return lhs->get_id() == rhs->get_id();
	}
};


typedef std::unordered_set<std::unique_ptr<handler>, handler_hashfunc, handler_equal> handler_set;
typedef std::map<uint32_t, std::unique_ptr<handler> > handler_map;

/* since I have to work with libev, hard to get away from raw pointers */
extern handler_map g_active_handlers;
extern handler_set g_inactive_handlers;

class connect_handler { /* for non-blocking connectors */
public:
	/* fd should be non-blocking socket. Connect has not been called yet */
	connect_handler(int fd, const struct sockaddr_in &remote_addr); 
	void io_cb(ev::io &watcher, int revents);
	~connect_handler();
private:
	struct sockaddr_in remote_addr_;
	ev::io io;
	void setup_handler(int fd);
	connect_handler & operator=(connect_handler other);
	connect_handler(const connect_handler &);
	connect_handler(const connect_handler &&other);
	connect_handler & operator=(connect_handler &&other);
};


class accept_handler {
public:
	accept_handler(int fd, const struct sockaddr_in &a_local_addr); /* fd should be a listening, non-blocking socket */
	void io_cb(ev::io &watcher, int revents);
	~accept_handler();
private:
	struct sockaddr_in local_addr; /* left in network byte order */
	ev::io io;
	accept_handler & operator=(accept_handler other);
	accept_handler(const accept_handler &);
	accept_handler(const accept_handler &&other);
	accept_handler & operator=(accept_handler &&other);
};

};

#endif
