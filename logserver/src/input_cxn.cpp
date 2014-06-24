#include "input_cxn.cpp"

#include <iostream>

using namespace std;

namespace input_cxn {

const uint32_t RECV_PAYLOAD = 0x1;
const uint32_t RECV_LOG = 0x2;

handler::handler(int fd) {
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
					to_read = ntoh((uint32_t) read_queue.raw_buffer());
					read_queue.reserve(to_read);
					read_queue.seek(0);
					state = RECV_LOG;
				} else {
					/* item needs to be handled */
					auto p = read_queue.extract(); /* read_queue just got zeroes out */
					g_write_queue.append(move(p.first), p.second);
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
