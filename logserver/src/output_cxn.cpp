#include "output_cxn.hpp"

#include <cassert>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>

#include <iostream>
#include <map>

#include "network.hpp"
#include "netwrap.hpp"
#include "collector.hpp"

using namespace std;

namespace output_cxn {

const uint32_t RECV_HEADER = 0x1;
const uint32_t RECV_LOG = 0x2;

static map<handlers::accept_handler<handler> *, uint8_t>  g_interests;


void handler::handle_accept_error(handlers::accept_handler<handler> *handler, const network_error &e) {
	cerr << "Giving up accepting\n";
	cerr << e.what() << endl;
	handler->io.stop();
	close(handler->io.fd);

	/* TODO: call some function that continually tries to reacquire output accept channel
	   and destroys this handler */
}

void handler::handle_accept(handlers::accept_handler<handler> *h, int fd) {
	new handler(fd, get_interests(h)); /* let him delete himself */
}

void handler::set_interest(handlers::accept_handler<handler> *h, uint8_t interest) {
	if (interest) {
		g_interests[h] = interest;
	} else {
		g_interests.erase(h);
	}
}

uint8_t handler::get_interests(handlers::accept_handler<handler> *h) {
	uint8_t rv = 0;
	auto it = g_interests.find(h);
	if (it != g_interests.end()) {
		rv = it->second;
	}
	return rv;
}

handler::handler(int fd, uint8_t _interests) 
	: interests(_interests), events(ev::NONE), write_queue(), io() {
	cerr << "Instantiating new output handler on fd " << fd << "\n";
	io.set<handler, &handler::io_cb>(this);
	io.set(fd, events);
	io.start(); 
	collector::get().add_consumer(this);
}


void handler::set_events(int events) { 
	if (events != this->events) {
		this->events = events;
		io.set(events);
		io.start();
	}
}

int handler::get_events() const {
	return events;
}

handler::~handler() {
	collector::get().retire_consumer(this);
	if (io.fd >= 0) {
		io.stop();
		close(io.fd);
	}
}

void handler::io_cb(ev::io &watcher, int revents) {
	if (revents & ev::WRITE) {
		ssize_t r(1);
		while (write_queue.to_write() && r > 0) { 
			pair<int,bool> res = write_queue.do_write(watcher.fd);
			r = res.first;
			
			if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) { 
				/* most probably a disconnect of some sort, log error and queue object for deletion */
				cerr << "Received error on write: " << strerror(errno) << endl;
				suicide();
				return;
			} 

			if (r == 0) {
				cerr << "Disconnect\n";
				suicide();
				return;
			}
		}

		if (write_queue.to_write() == 0) {

			struct sized_buffer p(collector::get().pop(this));
			if (p.len) {
				uint32_t id = hton(p.source_id);
				uint32_t len = hton((uint32_t)(p.len + sizeof(id)));
				write_queue.append((uint8_t*)&len, sizeof(len));
				write_queue.append((uint8_t*)&id, sizeof(id));
				write_queue.append(p.buffer, p.len);
			} else {
				this->events = ev::NONE;
				io.set(ev::NONE);
				io.stop();/* nothing to pop, so just wait */
			}
		}
	}
}	

void handler::suicide() {
	if (io.fd >= 0) {
		io.stop();
		close(io.fd);
		io.fd = -1;
		delete this; /* eek, gotta be on the heap. TODO: fixme */
	}
}


};
