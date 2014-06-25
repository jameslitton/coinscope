#ifndef OUTPUT_CXN_HPP
#define OUTPUT_CXN_HPP

#include <cstdint>

#include <set>

#include <ev++.h>

#include "iobuf.hpp"

namespace output_cxn {

class accept_handler {
public:
	accept_handler(int fd);
	void io_cb(ev::io &watcher, int revents);
	~accept_handler();
private:
	ev::io io;
};

class handler {
private:
	int events;
	iobuf write_queue; /* TODO this NEEDS to be smarter */
	size_t to_write;
	ev::io io;
public:
	handler(int fd);
	~handler();
	void io_cb(ev::io &watcher, int revents);
	void set_events(int events);
	int get_events() const;
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
