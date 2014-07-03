#include <iostream>
#include <iterator>
#include <sstream>
#include <set>

#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "command_handler.hpp"
#include "bitcoin_handler.hpp"
#include "netwrap.hpp"
#include "network.hpp"
#include "logger.hpp"

using namespace std;

namespace bc = bitcoin;

namespace ctrl {

handler_set g_active_handlers;

uint32_t handler::id_pool = 0;


class registered_msg {
public:
	time_t registration_time;
	uint32_t id;
	unique_ptr<struct bitcoin::packed_message, void(*)(void*)> msg;

	bool operator<(const registered_msg &other) const { return id < other.id; }
	registered_msg(time_t regtime, time_t newid, struct message *messg) 
		: registration_time(regtime), id(newid), 		
		  msg((struct bitcoin::packed_message *) malloc(sizeof(struct bitcoin::packed_message) + messg->length), free)
	{
		memcpy(msg.get(), messg, sizeof(struct bitcoin::packed_message) + messg->length);
	}
	registered_msg(registered_msg &&other) 
		: registration_time(other.registration_time),
		  id(other.id), msg(NULL,free)
	{
		msg.swap(other.msg);
	}
private:
	registered_msg & operator=(registered_msg other);
	registered_msg(const registered_msg &);
	registered_msg & operator=(registered_msg &&other);

};

set<registered_msg> g_messages;


void handler::handle_message_recv(const struct command_msg *msg) { 
	vector<uint8_t> out;


	if (msg->command == COMMAND_GET_CXN) {
		g_log<CTRL>("All connections requested", regid);
		/* format is 32 bit id (network byte order), struct version_packed_net_addr */
		uint8_t buffer[sizeof(uint32_t) + sizeof(in_addr)+sizeof(uint16_t)];
		for(auto it = bc::g_active_handlers.cbegin(); it != bc::g_active_handlers.cend(); ++it) {
			uint32_t nid = hton((*it)->get_id());
			memcpy(buffer, &nid, sizeof(nid));
			struct bc::version_packed_net_addr remote;
			remote.addr.ipv4.as.in_addr = (*it)->get_remote_addr();
			remote.addr.ipv4.padding[10] = remote.addr.ipv4.padding[11] = 0xFF;
			remote.port = (*it)->get_remote_port();
			memcpy(buffer+sizeof(nid), &remote, sizeof(remote));
		}
		out.reserve(sizeof(buffer));
		copy(buffer, buffer + sizeof(buffer), back_inserter(out));
		iobuf_spec::append(&write_queue, out.data(), out.size());
		state |= SEND_MESSAGE;
		to_write += out.size();
	}

}

void handler::receive_header() {
	/* interpret data as message header and get length, reset remaining */ 
	struct message *msg = (struct message*)read_queue.raw_buffer();
	to_read = ntoh(msg->length);
	if (msg->version != 0) {
		g_log<DEBUG>("Warning: Unsupported version");
		
	}
	if (to_read == 0) { /* payload is packed message */
		if (msg->message_type == REGISTER) {
			uint32_t oldid = regid;
			/* changing id and sending it. */
			regid = nonce_gen32();
			g_log<CTRL>("UNREGISTERING", oldid);
			g_log<CTRL>("REGISTERING", regid);
			write_queue.grow(write_queue.location() + 4);
			uint32_t netorder = hton(regid);
			write_queue.append(&netorder);
			to_write += 4;
			/* msg->payload should be zero length here */
			/* send back their new user id */
		} else {
			ostringstream oss("Unknown message: ");
			oss << msg;
			g_log<CTRL>(oss.str());
			/* command and bitcoin payload messages always have a payload */
		}

		read_queue.seek(0);
		to_read = sizeof(struct message);

	} else {
		state = (state & SEND_MASK) | RECV_PAYLOAD;
	}
}

void handler::receive_payload() {
	struct message *msg = (struct message*) read_queue.raw_buffer();

	if (msg->version != 0) {
		g_log<DEBUG>("Warning: unsupported version. Attempting to receive payload");
		
	}

	switch(msg->message_type) {
	case BITCOIN_PACKED_MESSAGE:
		/* register message and send back its id */
		{
			auto pair = g_messages.insert(registered_msg(time(NULL), nonce_gen32(), msg));
			g_log<CTRL>("Registering message ", regid, msg);
			if (pair.second) {
				write_queue.grow(write_queue.location() + 4);
				uint32_t netorder = hton(pair.first->id);
				write_queue.append(&netorder);
				to_write += 4;
				g_log<CTRL>("message registered", regid, pair.first->id);
			}
		}
		break;
	case COMMAND:
		handle_message_recv((struct command_msg*) msg->payload);
		state = (state & SEND_MASK) | RECV_HEADER;
		break;
	case CONNECT:
		{
			/* format is remote packed_net_addr, local packed_net_addr */
			/* currently local is ignored, but would be used if we bound to more than one interface */
			struct connect_payload *payload = (struct connect_payload*) msg->payload;
			g_log<CTRL>("Attempting to connect for", regid);

			int fd(-1);
			struct sockaddr_in addr;
			try {
				fd = Socket(AF_INET, SOCK_STREAM, 0);
				/* This socket is NOT non-blocking. Is this an issue in building up connections? */
				bzero(&addr,sizeof(addr));
				addr.sin_family = AF_INET;
				addr.sin_port = payload->remote.port;
				memcpy(&addr.sin_addr, &payload->remote.addr.ipv4.as.bytes, sizeof(struct in_addr));
				Connect(fd, (struct sockaddr*)&addr, sizeof(addr));
				fcntl(fd, F_SETFL, O_NONBLOCK);
			} catch (network_error &e) {
				g_log<ERROR>(e.what(), "(command_handler)");
			}
			if (fd >= 0) {
				bc::g_active_handlers.emplace(new bc::handler(fd, bc::SEND_VERSION_INIT, addr.sin_addr, addr.sin_port, payload->local.addr.ipv4.as.in_addr, payload->local.port));
			}
		}
		break;
	default:
		g_log<CTRL>("unknown payload type", regid, msg);
		break;
	}

	to_read = sizeof(struct message);
	state = (state & SEND_MASK) | RECV_HEADER;

}

void handler::do_read(ev::io &watcher, int revents) {
	ssize_t r(1);
	while(r > 0) { 
		do {
			read_queue.grow(read_queue.location() + to_read);
			r = recv(watcher.fd, read_queue.offset_buffer(), to_read, 0);
			if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN) { 
				g_log<ERROR>(strerror(errno), "(command_handler)");
			}
			if (r > 0) {
				to_read -= r;
				read_queue.seek(read_queue.location() + r);
			}

			if (r == 0) { /* got disconnected! */
				/* LOG disconnect */
				g_log<CTRL>("Orderly disconnect", id);
				io.stop();
				close(io.fd);
				io.fd = -1;
				g_active_handlers.erase(this);
				delete this;
				return;
			}
		} while (r > 0 && to_read > 0);

		if (to_read == 0) {
			/* item needs to be handled */
			switch(state & RECV_MASK) {
			case RECV_HEADER:
				receive_header();
				break;
			case RECV_PAYLOAD:
				receive_payload();
				break;
			default:
				cerr << "inconceivable!" << endl;
				break;
			}
			break;
		}
	}
}

void handler::do_write(ev::io &watcher, int revents) {

	ssize_t r(1);
	while (to_write && r > 0) {
		r = write(watcher.fd, write_queue.offset_buffer(), to_write);
		if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN) { 
			g_log<ERROR>(strerror(errno), "(command_handler)");
		}
		if (r > 0) {
			write_queue.seek(write_queue.location() + r);
		}
	}

	if (to_write == 0) {
		state &= ~SEND_MASK;
		write_queue.seek(0);
	}
}

void handler::io_cb(ev::io &watcher, int revents) {
	uint32_t old_state = state;

	if ((state & RECV_MASK) && (revents & ev::READ)) {
		do_read(watcher, revents);
	}
         
	if (revents & ev::WRITE) {
		do_write(watcher, revents);
	}

	int events = 0;
	if (state != old_state) {
		if (state & SEND_MASK) {
			events |= ev::WRITE;
		}
		if (state & RECV_MASK) {
			events |= ev::READ;
		}
		io.set(events);
	}
}


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
	struct sockaddr addr = {0};
	socklen_t len(0);
	int client;
	try {
		client = Accept(watcher.fd, &addr, &len);
	} catch (network_error &e) {
		if (e.error_code() != EWOULDBLOCK && e.error_code() != EAGAIN) {
			g_log<ERROR>(e.what(), "(command_handler)");
			watcher.stop();
			close(watcher.fd);
			delete this;

			/* not sure entirely what recovery policy should be on dead control channels, probably reattempt acquisition, this is a TODO */
			/*
			  if (watchers.erase(watcher)) {
			  delete this;
			  }
			*/
		}
		return;
	}

	g_active_handlers.emplace(new handler(client));
}

};

