#include "bitcoin_handler.hpp"

#include <iostream>

#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "netwrap.hpp"
#include "logger.hpp"

using namespace std;

namespace bitcoin {

handler_set g_active_handlers;

uint32_t handler::id_pool = 0;

accept_handler::accept_handler(int fd, struct in_addr a_local_addr, uint16_t a_local_port) 
	: local_addr(a_local_addr), local_port(a_local_port), io()
{
	g_log(INTERNALS) << "bitcoin accept initializer initiated, awaiting incoming client connections";
	io.set<accept_handler, &accept_handler::io_cb>(this);
	io.set(fd, ev::READ);
	io.start();
}

accept_handler::~accept_handler() {
	g_log(INTERNALS) << "bitcoin accept handler destroyed";
	io.stop();
	close(io.fd);
}

void accept_handler::io_cb(ev::io &watcher, int revents) {
	struct sockaddr_in addr;
	socklen_t len;
	int client;
	try {
		client = Accept(watcher.fd, (struct sockaddr*)&addr, &len);
		fcntl(client, F_SETFD, O_NONBLOCK);		
	} catch (network_error &e) {
		if (e.error_code() != EWOULDBLOCK && e.error_code() != EAGAIN && e.error_code() != EINTR) {
			cerr << e.what() << endl;
			/* trigger destruction of self via some kind of queue and probably recreate channel! */
		}
		return;
	}
	g_log(BITCOIN) << "Accepted connection to client on fd " << client << " " << *((struct sockaddr*)&addr);
	/* TODO: if can be converted to smarter pointers sensibly, consider, but
	   since libev doesn't use them makes it hard */
	g_active_handlers.emplace(new handler(client, RECV_VERSION_REPLY, addr.sin_addr, addr.sin_port, local_addr, local_port));
}

handler::handler(int fd, uint32_t a_state, struct in_addr a_remote_addr, uint16_t a_remote_port,
                 in_addr a_local_addr, uint16_t a_local_port) : 
	remote_addr(a_remote_addr), remote_port(a_remote_port),
	local_addr(a_local_addr), local_port(a_local_port), 
	state(a_state), 
	id(id_pool++) 
{
	char local_str[16];
	char remote_str[16];
	inet_ntop(AF_INET, &a_remote_addr, remote_str, sizeof(remote_str));
	inet_ntop(AF_INET, &a_local_addr, local_str, sizeof(local_str));
	g_log(BITCOIN) << "Initiating handler with state " << state << " on " 
	               << local_str << ":" << ntoh(a_local_port) 
	               << " with " << remote_str << ":" << ntoh(a_remote_port) 
	               << " with id " << id;

	io.set<handler, &handler::io_cb>(this);
	if (a_state == SEND_VERSION_INIT) {
		/* TODO: profile to see if extra copies are worth optimizing away */
		struct combined_version vers(get_version(USER_AGENT, local_addr, local_port, remote_addr, remote_port));
		unique_ptr<struct packed_message, void(*)(void*)> msg(get_message("version", vers.as_buffer(), vers.size));
		g_log(BITCOIN_MSG, id, true) << msg.get(); /* TODO: some discontinuity between queue time and transmit complete time */
		write_queue.append(msg.get());
		to_write += sizeof(struct packed_message)+msg->length;
		io.start(fd, ev::READ | ev::WRITE);
	} else {
		io.start(fd, ev::READ);
	}
}



void handler::handle_message_recv(const struct packed_message *msg) { 
	g_log(BITCOIN_MSG, id, false) << msg;
	if (strcmp(msg->command, "ping") == 0) {
		g_log(BITCOIN_MSG, id, true) << msg;
		write_queue.append(msg); /* it makes sense to just ferret this through a function and log all outgoing appends easy-peasy */
		to_write += sizeof(struct packed_message) + msg->length;
	}
}

handler::~handler() { 
	if (io.fd >= 0) {
		g_log(BITCOIN, id) << "Shutting down via destructor";
		io.stop();
		close(io.fd);
	}
}

void handler::suicide() {
	g_log(BITCOIN, id) << "Shutting down";
	io.stop();
	close(io.fd);
	io.fd = -1;
	g_active_handlers.erase(this);
	delete this;
}

void handler::io_cb(ev::io &watcher, int revents) {
	if ((state & RECV_MASK) && (revents & ev::READ)) {
		ssize_t r(1);
		while(r > 0) { /* do all reads we can in this event handler */
			do {
				r = read(watcher.fd, read_queue.offset_buffer(), to_read);
				if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) { 
					/* 
					   most probably a disconnect of some sort, though I
					   think with reads on a socket this should just come
					   across as a zero byte read, not an error... Anyway,
					   log error and queue object for deletion
					*/

					g_log(ERROR, id) << "Got unexpected error on handler. " << strerror(errno);
					suicide();
					return;

				}
				if (r > 0) {
					to_read -= r;
					read_queue.seek(read_queue.location() + r);
				}
				if (r == 0) { /* got disconnected! */
					/* LOG disconnect */
					g_log(BITCOIN, id) << "Remote disconnect";
					suicide();
					return;
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
					g_log(BITCOIN, id, false) << ((struct packed_message*) read_queue.raw_buffer());
					state = (state & SEND_MASK) | RECV_HEADER; 
					break;
				case RECV_VERSION_REPLY: // they initiated handshake, send our version and verack
					struct combined_version vers(get_version(USER_AGENT, remote_addr, remote_port, local_addr, local_port));
					(g_log(BITCOIN, id, true) << "VERS").write((const char*)vers.as_buffer(), vers.size);;
					write_queue.append(&vers);
					unique_ptr<struct packed_message, void(*)(void*)> msg(get_message("verack"));
					g_log(BITCOIN, id, true) << msg.get();
					write_queue.append(msg.get());
					if (!(state & SEND_MASK)) {
						io.set(ev::READ|ev::WRITE);
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

			if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) { 
				/* most probably a disconnect of some sort, log error and queue object for deletion */
				g_log(BITCOIN, id) << "Received error on write: " << strerror(errno);
				suicide();
				return;
			} 
			if (r > 0) {
				write_queue.seek(write_queue.location() + r);
			}
		}

		if (to_write == 0) {
			switch(state & SEND_MASK) {
			case SEND_VERSION_INIT:
				state = RECV_VERSION_INIT;
				break;
			default:
				/* we actually do no special handling here so we can
				   buffer up writes. Beyond SEND_VERSION_INIT, we don't
				   care why we are sending */
				break;
			}

			io.set(ev::READ);
			state &= ~SEND_MASK;
			write_queue.seek(0);


		}
         

	}

}
};

