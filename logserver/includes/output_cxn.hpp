#ifndef OUTPUT_CXN_HPP
#define OUTPUT_CXN_HPP

#include <cstdint>

#include <ev++.h>

#include "accept_handler.hpp"
#include "netwrap.hpp"
#include "write_buffer.hpp"

namespace output_cxn {

class handler {
private:
	uint8_t interests;
	int events;
	write_buffer write_queue;
	ev::io io;
public:
	handler(int fd, uint8_t interests);
	~handler();
	void io_cb(ev::io &watcher, int revents);
	void set_events(int events);
	int get_events() const;
	bool interested(uint8_t x) const { return x & interests; }

	/* for creation functions */
	static void set_interest(handlers::accept_handler<handler> *h, uint8_t interest);
	static uint8_t get_interests(handlers::accept_handler<handler> *h) ;
	static void handle_accept_error(handlers::accept_handler<handler> *handler, const network_error &e);
	static void handle_accept(handlers::accept_handler<handler> *handler, int fd);

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
