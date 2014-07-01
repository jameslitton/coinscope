#ifndef BITCOIN_HANDLER_HPP
#define BITCOIN_HANDLER_HPP

#include <cstdint>

#include <unordered_set>
#include <string>

#include <netinet/in.h>

#include <ev++.h>

#include "iobuf.hpp"

namespace bitcoin {


const uint32_t RECV_MASK = 0x0000ffff; // all receive flags should be in the mask 
const uint32_t RECV_HEADER = 0x1;
const uint32_t RECV_PAYLOAD = 0x2;
const uint32_t RECV_VERSION_INIT = 0x4; /* we initiated the handshake, now waiting to receive version */
const uint32_t RECV_VERSION_REPLY_HDR = 0x8;
const uint32_t RECV_VERSION_REPLY = 0x10;

const uint32_t SEND_MASK = 0xffff0000;
const uint32_t SEND_MESSAGE = 0x10000;
const uint32_t SEND_VERSION_INIT = 0x20000; /* we initiated the handshake */
const uint32_t SEND_VERSION_REPLY = 0x40000;
   

const std::string USER_AGENT("/Satoshi:0.9.2/");


class handler {
private:
	iobuf read_queue; /* application needs to read and act on this data */
	size_t to_read;

	iobuf write_queue; /* application wants this written out across network */
	size_t to_write;

	in_addr remote_addr;
	uint16_t remote_port;

	in_addr local_addr; /* address we connected on */
	uint16_t local_port;

	uint32_t state;

	ev::io io;

	uint32_t id;
	static uint32_t id_pool;

public:
	handler(int fd, uint32_t a_state, struct in_addr a_remote_addr, uint16_t a_remote_port,
	        in_addr a_local_addr, uint16_t a_local_port);
	~handler();
	uint32_t get_id() const { return id; }
	void handle_message_recv(const struct packed_message *msg);
	void io_cb(ev::io &watcher, int revents);
	struct in_addr get_remote_addr() const { return remote_addr; }
	uint16_t get_remote_port() const { return remote_port; }
private:
	void suicide(); /* get yourself ready for suspension (e.g., stop loop activity) if safe, just delete self */
	/* could implement move operators, but others are odd */
	handler & operator=(handler other);
	handler(const handler &);
	handler(const handler &&other);
	handler & operator=(handler &&other);
	void do_read(ev::io &watcher, int revents);
	void do_write(ev::io &watcher, int revents);
};

struct handler_hashfunc {
	size_t operator()(const handler *h) const {
		return std::hash<uint32_t>()(h->get_id());
	}
};

struct handler_equal {
	bool operator()(const handler* lhs, const handler* rhs) const {
		return lhs->get_id() == rhs->get_id();
	}
};


typedef std::unordered_set<handler*, handler_hashfunc, handler_equal> handler_set;
/* since I have to work with libev, hard to get away from raw pointers */
extern handler_set g_active_handlers;


class accept_handler {
public:
	accept_handler(int fd, struct in_addr a_local_addr, uint16_t a_local_port); /* fd should be a listening, non-blocking socket */
	void io_cb(ev::io &watcher, int revents);
	~accept_handler();
private:
	struct in_addr local_addr; /* left in network byte order */
	uint16_t local_port;
	ev::io io;
	accept_handler & operator=(accept_handler other);
	accept_handler(const accept_handler &);
	accept_handler(const accept_handler &&other);
	accept_handler & operator=(accept_handler &&other);
};

};

#endif
