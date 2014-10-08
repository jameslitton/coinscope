#include "bitcoin_handler.hpp"

#include <cassert>

#include <iostream>
#include <sstream>
#include <set>

#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "netwrap.hpp"
#include "logger.hpp"
#include "config.hpp"
#include "crypto.hpp"

using namespace std;

namespace bitcoin {

handler_map g_active_handlers;
handler_set g_inactive_handlers;

uint32_t handler::id_pool = 0;

static int g_ping_iv = -1;


static set<connect_handler *> g_inactive_connection_handlers; /* to be deleted */

connect_handler::connect_handler(int fd, const struct sockaddr_in &remote_addr) 
	: remote_addr_(remote_addr), io() 
{

	/* clean up old dead ones. There's a logic to this scheme, dumb as it is. Ask
	   if you want to hear it. */
	for(auto it = g_inactive_connection_handlers.begin(); it != g_inactive_connection_handlers.end(); ++it) {
		delete *it;
	}

	g_inactive_connection_handlers.clear();

	/* first try it */
	int rv = connect(fd, (struct sockaddr*)&remote_addr_, sizeof(remote_addr_));
	if (rv == 0) {
		g_log<DEBUG>("No need to do non-blocking connect, setup was instant");
		setup_handler(fd);
		g_inactive_connection_handlers.insert(this);
	} else if (errno == EINPROGRESS || errno == EALREADY) {
		io.set<connect_handler, &connect_handler::io_cb>(this);
		io.set(fd, ev::WRITE); /* mark as writable once the connection comes in */
		io.start();
	}
}

void connect_handler::setup_handler(int fd) { 
	struct sockaddr_in local;
	socklen_t len = sizeof(local);
	bzero(&local,sizeof(local));
	if (getsockname(fd, (struct sockaddr*) &local, &len) != 0) {
		g_log<ERROR>(strerror(errno));
	} 
	unique_ptr<handler> h(new handler(fd, SEND_VERSION_INIT, remote_addr_, local));
	g_active_handlers.insert(make_pair(h->get_id(), move(h)));
}

void connect_handler::io_cb(ev::io &watcher, int /*revents*/) {
	int rv = connect(watcher.fd, (struct sockaddr*)&remote_addr_, sizeof(remote_addr_));

	bool is_inactive = false;

	if (rv == 0) {
		setup_handler(watcher.fd);
		io.stop();
		io.fd = -1; // Do not close. handler in setup_handler owns fd now 
		is_inactive = true;
	} else if (errno == EALREADY || errno == EINPROGRESS) {
		/* spurious event. */
	} else {
		char *err = strerror(errno);
		uint32_t len = strlen(err);
		io.stop();
		close(io.fd);
		io.fd = -1;
		is_inactive = true;
		struct sockaddr_in local;
		bzero(&local, sizeof(local));
		local.sin_family = AF_INET; /* there is no local connection actually */
		g_log<BITCOIN>(CONNECT_FAILURE, 0, remote_addr_, local, err, len+1);
	}
	
	if (is_inactive) {
		for(auto it = g_inactive_connection_handlers.begin(); it != g_inactive_connection_handlers.end(); ++it) {
			delete *it;
		}
		g_inactive_connection_handlers.clear();
		g_inactive_connection_handlers.insert(this);
	}
}

connect_handler::~connect_handler() { 
	/* naturally the file descriptor need not be closed. It belongs to the
	   bc::handler now, if anyone. */
	assert(! io.is_active());
}


accept_handler::accept_handler(int fd, const struct sockaddr_in &a_local_addr)
	: local_addr(a_local_addr), io()
{
	g_log<DEBUG>("bitcoin accept initializer initiated, awaiting incoming client connections");
	io.set<accept_handler, &accept_handler::io_cb>(this);
	io.set(fd, ev::READ);
	io.start();
}

accept_handler::~accept_handler() {
	g_log<DEBUG>("bitcoin accept handler destroyed");
	io.stop();
	close(io.fd);
	io.fd = -1;
}

void accept_handler::io_cb(ev::io &watcher, int /*revents*/) {
	struct sockaddr_in addr;
	socklen_t len(sizeof(addr));
	int client;
	try {
		client = Accept(watcher.fd, (struct sockaddr*)&addr, &len);
		fcntl(client, F_SETFL, O_NONBLOCK);		
	} catch (network_error &e) {
		if (e.error_code() != EWOULDBLOCK && e.error_code() != EAGAIN && e.error_code() != EINTR) {
			g_log<ERROR>(e.what(), "(bitcoin_handler)", e.error_code());
			
			/* trigger destruction of self via some kind of queue and probably recreate channel! */
		}
		return;
	}

	sockaddr_in local;
	socklen_t socklen = sizeof(local);
	bzero(&local,sizeof(local));
	if (getsockname(client, (struct sockaddr*) &local, &socklen) != 0) {
		g_log<ERROR>(strerror(errno));
	} 

	/* TODO: if can be converted to smarter pointers sensibly, consider, but
	   since libev doesn't use them makes it hard */
	unique_ptr<handler> h(new handler(client, RECV_VERSION_REPLY_HDR, addr, local));
	g_active_handlers.insert(make_pair(h->get_id(), move(h)));
	g_inactive_handlers.clear();
}


handler::handler(int fd, uint32_t a_state, const struct sockaddr_in &a_remote_addr, const struct sockaddr_in &a_local_addr) 
	: read_queue(0),
	  write_queue(),
	  remote_addr(a_remote_addr),
	  local_addr(a_local_addr),
	  timestamp(ev::now(ev_default_loop())),
	  state(a_state), 
	  io(), timer(), last_activity(timestamp),
	  id(id_pool++) 
{

	ostringstream oss;
	
	io.set<handler, &handler::io_cb>(this);
	if (a_state == SEND_VERSION_INIT) { /* we initiated the connection */
		io.set(fd, ev::WRITE);
		/* TODO: profile to see if extra copies are worth optimizing away */
		struct combined_version vers(get_version(USER_AGENT, local_addr, remote_addr));
		unique_ptr<struct packed_message> m(get_message("version", vers.as_buffer(), vers.size));
		g_log<BITCOIN_MSG>(id, true, m.get());
		write_queue.append((const uint8_t *) m.get(), m->length + sizeof(*m));
		g_log<BITCOIN>(CONNECT_SUCCESS, id, remote_addr, local_addr, NULL, 0);
	} else if (a_state == RECV_VERSION_REPLY_HDR) { /* they initiated did */
		io.set(fd, ev::READ);
		read_queue.to_read(sizeof(struct packed_message));
		g_log<BITCOIN>(ACCEPT_SUCCESS, id, remote_addr, local_addr, NULL, 0);
	}
	assert(io.fd > 0);
	io.start();

	if (g_ping_iv < 0) {
		const libconfig::Config *cfg(get_config());
		int pf = cfg->lookup("connector.bitcoin.ping_frequency");
		g_ping_iv = max(0, pf);
	}

	if (g_ping_iv > 0) {
		timer.set<handler, &handler::pinger_cb>(this);
		timer.set(g_ping_iv);
		timer.start();
	}
	
}

void handler::pinger_cb(ev::timer &/*w*/, int /*revents*/) {
	ev::tstamp after = last_activity - ev::now(ev_default_loop()) + g_ping_iv;
	if (after < 0.0) {
		uint64_t nonce = nonce_gen64();
		auto m(get_message("ping", (uint8_t*)&nonce, 8));
		append_for_write(move(m));
		timer.stop();
		timer.set(g_ping_iv);
		timer.start();
	} else {
		timer.stop();
		timer.set(after);
		timer.start();
	}
}


void handler::handle_message_recv(const struct packed_message *msg) { 
	g_log<BITCOIN_MSG>(id, false, msg);
	if (strcmp(msg->command, "ping") == 0) {
		struct packed_message *pong = static_cast<struct packed_message*>(alloca(sizeof(pong) + msg->length));
		memcpy(pong, msg, sizeof(*pong) + msg->length);
		pong->command[1] = 'o';
		append_for_write(pong);
	} else if (strcmp(msg->command, "getblocks") == 0) {
		vector<uint8_t> payload(get_inv(vector<inv_vector>()));
		append_for_write(get_message("inv", payload));
	} else if (false && strcmp(msg->command, "getaddr") == 0) { /* need to be careful about pollution, placeholder */
		// see commit 9f30aa21efe3080b004d5a48ef7be46e9b88e9a5 for placeholder code here 
	} else if (strcmp(msg->command, "version") == 0) {
		/* start height is 5 bytes from the end... */
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
		int32_t given_block = *((int32_t*) (msg->payload + msg->length - 5));
#pragma GCC diagnostic warning "-Wstrict-aliasing"
		//cerr << "given block is " << given_block << "\n";
		if (given_block < 500000 && given_block > g_last_block) {
			/* TODO: correct behavior? */
			// There are some weird big given blocks out there.
			g_last_block = given_block;
		}

	}
}

handler::~handler() { 
	if (io.fd >= 0) {
		/* This shouldn't normally ever be destructed unless it is in the inactive_handler set, so this path shouldn't happen, but if so, don't leak */
		io.stop();
		close(io.fd);
		timer.stop();
		io.fd = -1;
		g_active_handlers.erase(id);
	}
}

void handler::disconnect() {
	g_log<BITCOIN>(CONNECTOR_DISCONNECT, id, remote_addr, local_addr, nullptr, 0);
	suicide();
}

void handler::suicide() {
	timer.stop();
	io.stop();
	close(io.fd);
	io.fd = -1;
	if (g_active_handlers.find(id) == g_active_handlers.end()) {
		cerr << "That's not supposed to happen\n";
	} else {
		unique_ptr<handler> ptr(move(g_active_handlers[id]));
		g_active_handlers.erase(id);
		/* delete everyone but me */
		g_inactive_handlers.clear();
		g_inactive_handlers.emplace(move(ptr)); 
	}
}

void handler::append_for_write(const struct packed_message *m) {
	g_log<BITCOIN_MSG>(id, true, m);
	write_queue.append((const uint8_t *) m, m->length + sizeof(*m));

	if (!(state & SEND_MASK)) { /* okay, need to add to the io state */
		int events = ev::WRITE | (state & RECV_MASK ? ev::READ : ev::NONE);
		io.set(events);
	}
	state |= SEND_MESSAGE;
}

void handler::append_for_write(wrapped_buffer<uint8_t> buf) {
	const  struct packed_message *m = (const struct packed_message*) buf.const_ptr();
	g_log<BITCOIN_MSG>(id, true, m);
	write_queue.append(buf, m->length + sizeof(*m));

	if (!(state & SEND_MASK)) { /* okay, need to add to the io state */
		int events = ev::WRITE | (state & RECV_MASK ? ev::READ : ev::NONE);
		io.set(events);
	}
	state |= SEND_MESSAGE;
}

void handler::append_for_write(unique_ptr<struct packed_message> m) {
	return append_for_write(m.get());
}

void handler::do_read(ev::io &watcher, int /* revents */) {
	assert(watcher.fd >= 0);
	ssize_t r(1);
	while(r > 0 && read_queue.hungry()) { /* do all reads we can in this event handler */
		while (r > 0 && read_queue.hungry()) {
			pair<int,bool> res(read_queue.do_read(watcher.fd));
			r = res.first;
			if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) { 
				/* 
				   most probably a disconnect of some sort, though I
				   think with reads on a socket this should just come
				   across as a zero byte read, not an error... Anyway,
				   log error and queue object for deletion
				*/
				if (errno == ECONNRESET) {
					g_log<BITCOIN>(PEER_RESET, id, remote_addr, local_addr, NULL, 0);
				} else {
					char *err = strerror(errno);
					g_log<BITCOIN>(UNEXPECTED_ERROR, id, remote_addr, local_addr, err, strlen(err)+1);
				}
				suicide();
				return;

			}
			if (r == 0) { /* got disconnected! */
				/* LOG disconnect */
				g_log<BITCOIN>(ORDERLY_DISCONNECT, id, remote_addr, local_addr, NULL, 0);
				suicide();
				return;
			}
		}

		if (!read_queue.hungry()) { /* all read up */
			wrapped_buffer<uint8_t> readbuf(read_queue.extract_buffer());

			assert(readbuf.allocated() >= sizeof(struct packed_message));
			const struct packed_message *msg = (struct packed_message *) readbuf.const_ptr();
			if (!(state & (RECV_HEADER|RECV_VERSION_INIT_HDR|RECV_VERSION_REPLY_HDR))) {
				assert(readbuf.allocated() >= sizeof(struct packed_message) + msg->length);
			}

			/* item needs to be handled */
			switch(state & RECV_MASK) {
			case RECV_HEADER:
				/* interpret data as message header and get length, reset remaining */ 
				read_queue.to_read(msg->length);
				if (!read_queue.hungry()) { /* payload is packed message */
					handle_message_recv((const struct packed_message*) readbuf.const_ptr());
					read_queue.cursor(0);
					read_queue.to_read(sizeof(struct packed_message));
				} else {
					state = (state & SEND_MASK) | RECV_PAYLOAD;
				}
				break;
			case RECV_PAYLOAD:
				handle_message_recv(msg);
				read_queue.cursor(0);
				read_queue.to_read(sizeof(struct packed_message));
				state = (state & SEND_MASK) | RECV_HEADER;
				break;
			case RECV_VERSION_INIT_HDR:
				read_queue.to_read(msg->length);
				state = (state & SEND_MASK) | RECV_VERSION_INIT;
				break;
			case RECV_VERSION_INIT: // we initiated handshake, we expect ack
				// next message should be zero length header with verack command
				handle_message_recv(msg);
				state = (state & SEND_MASK) | RECV_HEADER;
				read_queue.cursor(0);
				read_queue.to_read(sizeof(struct packed_message));
				break;
			case RECV_VERSION_REPLY_HDR: // they initiated the handshake, but we've only read the header
				read_queue.to_read(msg->length);
				state = (state & SEND_MASK) | RECV_VERSION_REPLY;
				break;
			case RECV_VERSION_REPLY: // they initiated handshake, send our version and verack
				handle_message_recv(msg);
				read_queue.cursor(0);
				read_queue.to_read(sizeof(struct packed_message));
					
				struct combined_version vers(get_version(USER_AGENT, remote_addr, local_addr));
				unique_ptr<struct packed_message> vmsg(get_message("version", vers.as_buffer(), vers.size));

				append_for_write(move(vmsg));
				append_for_write(get_message("verack"));
				state = SEND_VERSION_REPLY | RECV_HEADER;
				break;
			}

		}
	}
         

}

void handler::do_write(ev::io &watcher, int /*revents*/) {

	ssize_t r(1);
	while (write_queue.to_write() && r > 0) { 
		pair<int,bool> res = write_queue.do_write(watcher.fd);
		r = res.first;
		if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) { 
			/* most probably a disconnect of some sort, log error and queue object for deletion */
			char *err = strerror(errno);
			g_log<BITCOIN>(WRITE_DISCONNECT, id, remote_addr, local_addr, err, strlen(err)+1);
			suicide();
			return;
		} 
	}

	if (write_queue.to_write() == 0) {
		switch(state & SEND_MASK) {
		case SEND_VERSION_INIT:
			state = RECV_VERSION_INIT_HDR;
			assert(read_queue.to_read() == 0);
			read_queue.to_read(sizeof(struct packed_message)); 
			break;
		case SEND_VERSION_REPLY:
			break;
		default:
			/* we actually do no special handling here so we can
			   buffer up writes. Beyond SEND_VERSION_INIT, we don't
			   care why we are sending */
			break;
		}

		state &= RECV_MASK;

	}
         

}

void handler::io_cb(ev::io &watcher, int revents) {

	if (io.fd == -1) {
		if (g_active_handlers.find(id) != g_active_handlers.end()) {
			g_log<DEBUG>("Handler ", id, " has invalid file descriptor but is in active set");
			unique_ptr<handler> ptr(move(g_active_handlers[id]));
			g_active_handlers.erase(id);
			g_inactive_handlers.emplace(move(ptr)); 
		}
		return;
	}

	if ((state & RECV_MASK) && (revents & ev::READ)) {
		do_read(watcher, revents);
	}
        
	if (revents & ev::WRITE) {
		do_write(watcher, revents);
		
	}

	int events = 0;
	if (state & SEND_MASK) {
		events |= ev::WRITE;
	}
	if (state & RECV_MASK) {
		events |= ev::READ;
	}
	io.set(events);

	last_activity = ev::now(ev_default_loop());
}

};

