#ifndef COMMAND_HANDLER_HPP
#define COMMAND_HANDLER_HPP

#include <cstdint>

#include <unordered_set>

#include <ev++.h>

#include "command_structures.hpp"
#include "iobuf.hpp"

namespace ctrl {

const uint32_t RECV_MASK = 0x0000ffff; // all receive flags should be in the mask 
const uint32_t RECV_HEADER = 0x1;
const uint32_t RECV_PAYLOAD = 0x2;

const uint32_t SEND_MASK = 0xffff0000;
const uint32_t SEND_MESSAGE = 0x10000;



class handler {
private:
	iobuf read_queue;
	size_t to_read;

	iobuf write_queue; /* most logging should be through logging facility/filter */
	size_t to_write;

	uint32_t state;

	static uint32_t id_pool;

	uint32_t id;

	ev::io io;

public:
	handler(int fd) : to_read(sizeof(struct message)),state(RECV_HEADER), id(id_pool++) {
		io.set<handler, &handler::io_cb>(this);
		io.set(fd, ev::READ);
		io.start();
	}
	uint32_t get_id() const { return id; }
	void handle_message_recv(const struct command_msg *msg);
	void io_cb(ev::io &watcher, int revents);
	~handler() { 
		if (io.fd >= 0) {
			io.stop();
			close(io.fd);
		}
	}
private:
	handler & operator=(handler other);
	handler(const handler &);
	handler(const handler &&other);
	handler & operator=(handler &&other);
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
	accept_handler(int fd);
	void io_cb(ev::io &watcher, int revents);
	~accept_handler();
private:
	ev::io io;
	accept_handler & operator=(accept_handler other);
	accept_handler(const accept_handler &);
	accept_handler(const accept_handler &&other);
	accept_handler & operator=(accept_handler &&other);
};


#ifdef __cplusplus
};
#endif


#endif
