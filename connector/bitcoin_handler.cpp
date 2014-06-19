#include "bitcoin_handler.hpp"

#include <iostream>

#include <unistd.h>
#include <arpa/inet.h>



using namespace std;

namespace bitcoin {

void handler::handle_message_recv(const struct packed_message *msg) { 
	cout << "RMSG " << inet_ntoa(remote_addr) << ' ' << msg->command << ' ' << msg->length << endl;
	if (strcmp(msg->command, "ping") == 0) {
		write_queue.append(msg); /* it makes sense to just ferret this through a function and log all outgoing appends easy-peasy */
	}
}

void handler::io_cb(ev::io &watcher, int revents) {
	if ((state & RECV_MASK) && (revents & ev::READ)) {
		ssize_t r(1);
		while(r > 0) { /* do all reads we can in this event handler */
			do {
				r = read(watcher.fd, read_queue.offset_buffer(), to_read);
				if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN) { cerr << strerror(errno) << endl; }
				if (r > 0) {
					to_read -= r;
					read_queue.seek(read_queue.location() + r);
				}
			} while (r > 0 && to_read > 0);

			if (to_read == 0) {
				/* item needs to be handled */
				switch(state & RECV_MASK) {
				case RECV_HEADER:
					/* interpret data as message header and get length, reset remaining */ 
					to_read = ((struct packed_message*) read_queue.raw_buffer())->length;
					if (to_read == 0) { /* payload is packed message */
						handle_message_recv((struct packed_message*) read_queue.raw_buffer());
						read_queue.seek(0);
						to_read = sizeof(struct packed_message);
					} else {
						read_queue.reserve(sizeof(struct packed_message) + to_read);
						state = (state & SEND_MASK) | RECV_PAYLOAD;
					}
					break;
				case RECV_PAYLOAD:
					handle_message_recv((struct packed_message*) read_queue.raw_buffer());
					read_queue.seek(0);
					to_read = sizeof(struct packed_message);
					state = (state & SEND_MASK) | RECV_HEADER;
					break;
				case RECV_VERSION_INIT: // we initiated handshake, we expect ack
					// next message should be zero length header with verack command
					state = (state & SEND_MASK) | RECV_HEADER; 
					break;
				case RECV_VERSION_REPLY: // they initiated handshake, send our version and verack
					struct combined_version vers(get_version(USER_AGENT, remote_addr, remote_port, this_addr, this_port));
					write_queue.append(&vers);
					unique_ptr<struct packed_message, void(*)(void*)> msg(get_message("verack"));
					write_queue.append(msg.get());
					if (!(state & SEND_MASK)) {
						/* add to write event */
					}
					state = (state & SEND_MASK) | SEND_VERSION_REPLY | RECV_HEADER;
					break;
				}

			}
		}
         
	}

	if (revents & ev::WRITE) {

		ssize_t r(1);
		while (to_write && r > 0) {
			r = write(watcher.fd, write_queue.offset_buffer(), to_write);
			if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN) { cerr << strerror(errno) << endl; }
			if (r > 0) {
				write_queue.seek(write_queue.location() + r);
			}
		}

		if (to_write == 0) {
			switch(state & SEND_MASK) {
			case SEND_VERSION_INIT:
				/* set to fire watch read events */
				state = RECV_VERSION_INIT;
				break;
			default:
				/* we actually do no special handling here so we can
				   buffer up writes. Beyond SEND_VERSION_INIT, we don't
				   care why we are sending */
				break;
			}

			/* unregister write event! */
			state &= ~SEND_MASK;
			write_queue.seek(0);


		}
         

	}

}
};

