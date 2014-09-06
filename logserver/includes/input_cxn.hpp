#ifndef INPUT_CXN_HPP
#define INPUT_CXN_HPP

#include <cstdint>

#include <ev++.h>

#include "read_buffer.hpp"
#include "netwrap.hpp"
#include "accept_handler.hpp"

namespace input_cxn {

class handler {
private:
	read_buffer read_queue;
	uint32_t state;
	ev::io io;
	uint32_t id;
public:
	handler(int fd);
	~handler();
	void io_cb(ev::io &watcher, int revents);
	static void handle_accept_error(handlers::accept_handler<handler> *handler, const network_error &e);
	static void handle_accept(handlers::accept_handler<handler> *handler, int fd);
	uint32_t get_id() const { return id; }
private:
	void suicide(); /* get yourself ready for suspension (e.g., stop loop activity) if safe, just delete self */
	/* could implement move operators, but others are odd */
	handler & operator=(handler other);
	handler(const handler &);
	handler(const handler &&other);
	handler & operator=(handler &&other);
};

};
#endif
