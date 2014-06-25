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

accept_handler::accept_handler(int fd) {
	io.set<accept_handler, &accept_handler::io_cb>(this);
	io.set(fd, ev::READ);
	io.start();
}

accept_handler::~accept_handler() {
	io.stop();
	close(io.fd);
}

void accept_handler::io_cb(ev::io &watcher, int revents) {
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
	new handler(client); /* TODO, put him in a container, because they have to all be notified on new additions  */
}




handler::handler(int fd) : id(collector::get().add_consumer()) {
	cerr << "Instantiating new input handler\n";
	io.set<handler, &handler::io_cb>(this);
	io.start(fd, ev::WRITE);
}

handler::~handler() {
	collector::get().retire_consumer(id);
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

			shared_ptr<cvector<uint8_t> > p = collector::get().pop(id);
			if (p) {
				/* TODO: fix to not copy */
				iobuf_spec::append(&write_queue, p->data(), p->size());
				io.set(ev::WRITE);
				to_write = p->size();
			} else {
				io.set(0); /* TODO: add thing to reset IO !!*/
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


};
