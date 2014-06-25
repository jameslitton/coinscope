#include "output_cxn.hpp"

#include <cstring>
#include <unistd.h>
#include <fcntl.h>

#include <iostream>

#include "network.hpp"
#include "netwrap.hpp"
#include "collector.hpp"

using namespace std;

namespace output_cxn {

const uint32_t RECV_HEADER = 0x1;
const uint32_t RECV_LOG = 0x2;

accept_handler::accept_handler(int fd) : io(){
	io.set<accept_handler, &accept_handler::io_cb>(this);
	io.set(fd, ev::READ);
	io.start();
}

accept_handler::~accept_handler() {
	io.stop();
	close(io.fd);
}

void accept_handler::io_cb(ev::io &watcher, int /*revents*/) {
	int client;
	try {
		client = Accept(watcher.fd, NULL, NULL);
		fcntl(client, F_SETFD, O_NONBLOCK);		
	} catch (network_error &e) {
		if (e.error_code() != EWOULDBLOCK && e.error_code() != EAGAIN && e.error_code() != EINTR) {
			cerr << e.what() << endl;
			/* trigger destruction of self via some kind of queue and probably recreate channel! */
		}
		return;
	}
}


handler::handler(int fd) 
	: events(ev::NONE), write_queue(), to_write(0), io() {
	cerr << "Instantiating new input handler\n";
	io.set(fd);
	io.set<handler, &handler::io_cb>(this);
	collector::get().add_consumer(this);
	io.start(fd, events); 
}

void handler::set_events(int events) { 
	if (events != this->events) {
		this->events = events;
		io.set(events);
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
		while (to_write && r > 0) { 
			r = write(watcher.fd, write_queue.offset_buffer(), to_write);
			
			if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) { 
				/* most probably a disconnect of some sort, log error and queue object for deletion */
				cerr << "Received error on write: " << strerror(errno);
				suicide();
				return;
			} 
			if (r > 0) {
				write_queue.seek(write_queue.location() + r);
			}
		}

		if (to_write == 0) {

			write_queue.seek(0);

			shared_ptr<cvector<uint8_t> > p = collector::get().pop(this);
			if (p) {
				/* TODO: fix to not copy */
				uint32_t len = htonl(p->size());
				write_queue.append(&len);
				iobuf_spec::append(&write_queue, p->data(), p->size());
				io.set(ev::WRITE);
				to_write = p->size() + sizeof(len);
			} else {
				io.set(ev::NONE);
			}
		}
	}
}	

void handler::suicide() {
	io.stop();
	close(io.fd);
	io.fd = -1;
	delete this; /* eek, gotta be on the heap. TODO: fixme */
}


};
