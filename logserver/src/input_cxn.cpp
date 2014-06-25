#include "input_cxn.hpp"

#include <cstring>
#include <unistd.h>
#include <fcntl.h>

#include <iostream>

#include "network.hpp"
#include "netwrap.hpp"
#include "collector.hpp"

using namespace std;

namespace input_cxn {

const uint32_t RECV_HEADER = 0x1;
const uint32_t RECV_LOG = 0x2;

void handler::handle_accept_error(handlers::accept_handler<handler> *handler, const network_error &e) {
	cerr << e.what() << endl;
	handler->io.stop();
	close(handler->io.fd);

	/* TODO: call some function that continually tries to reacquire input channel
	   and destroys this handler */
}

void handler::handle_accept(handlers::accept_handler<handler> *, int fd) {
	new handler(fd); /* let him delete himself */
}


handler::handler(int fd) 
	: read_queue(), to_read(4), state(RECV_HEADER), io() {
	cerr << "Instantiating new input handler\n";
	io.set<handler, &handler::io_cb>(this);
	io.start(fd, ev::READ);
}

void handler::io_cb(ev::io &watcher, int revents) {
	if (revents & ev::READ) {
		ssize_t r(1);
		while(r > 0) { /* do all reads we can in this event handler */
			do {
				r = read(watcher.fd, read_queue.offset_buffer(), to_read);
				if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) { 
					cerr << "Got unexpected error on handler. " << strerror(errno);
					suicide();
					return;
				}
				if (r > 0) {
					to_read -= r;
					read_queue.seek(read_queue.location() + r);
				}
				if (r == 0) { /* got disconnected! */
					/* LOG disconnect */
					cerr << "Remote disconnect";
					suicide();
					return;
				}
			} while (r > 0 && to_read > 0);

			if (to_read == 0) {
				if (state == RECV_HEADER) {
					to_read = ntoh(*((uint32_t*) read_queue.raw_buffer()));
					read_queue.grow(to_read);
					read_queue.seek(0);
					state = RECV_LOG;
				} else {
					/* item needs to be handled */
					cvector<uint8_t> p = read_queue.extract(read_queue.location()); /* read_queue just got zeroes out */
					collector::get().append(move(p));
					to_read = 4;
					state = RECV_HEADER;
				}
			}
		}
	}
}


void handler::suicide() {
	io.stop();
	close(io.fd);
	io.fd = -1;
	delete this;
}


handler::~handler() { 
	if (io.fd >= 0) {
		io.stop();
		close(io.fd);
	}
}


};
