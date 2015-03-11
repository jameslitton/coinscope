#include <iostream>
#include <iterator>
#include <sstream>
#include <map>
#include <algorithm>

#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "groundctrl_command_handler.hpp"
#include "netwrap.hpp"
#include "network.hpp"
#include "logger.hpp"

using namespace std;

namespace bc = bitcoin;

namespace ctrl {

#define SOURCE_BITS 3

map<uint32_t, uint32_t> g_source_mask; /* for v0 people, mapping source_id to higher SOURCE_BITS mask */

uint32_t handler::id_pool = 0; /* must all fit in [0, 2^SOURCE_BITS] if we want v0 compatibility, otherwise full range */

map<uint32_t, map<uint32_t, uint32_t> > g_messages /* handle_id, local_msg_id, remote_msg_id */

handler::~handler() {
	g_messages.erase(id);
	if (io.fd >= 0) {
		close(io.fd);
		io.stop();
	}
}

void handler::handle_message_recv(const struct command_msg *msg, uint8_t version) { 
	vector<uint8_t> out;

	if (msg->command == COMMAND_GET_CXN) {
		/* GC ask all connector for their connections and send them all back. Take version into account!  */
		g_log<CTRL>("All connections requested", regid);
		/* format is struct connection_info or struct connection_info_v1 */

		/* call each connector command_get_cxn and pack in source_id, get the results (async?), and send back the results */
		write_queue.append(buffer, sizeof(len) + bc::g_active_handlers.size() * sizeof(struct connection_info));
		state |= SEND_MESSAGE;

	} else if (msg->command == COMMAND_SEND_MSG) {
		/* GC for each connector, map message_id to connector message_id and relay transformed message. Version means use a different foreach_handlers function */
		uint32_t message_id = ntoh(msg->message_id);
		auto it = g_messages[this->id].find(message_id);
		if (it == g_messages[this->id].end()) {
			g_log<ERROR>("invalid message id", message_id);
		} else {
			wrapped_buffer<uint8_t> packed(it->second.get_buffer());
			foreach_handlers(msg, [&](pair<const uint32_t, unique_ptr<bc::handler> > &p) {
					p.second->append_for_write(packed);
				});
		}
	} else if (msg->command == COMMAND_DISCONNECT) {
		/* GC Go to the connector that has a given id and issue the disconnect. Perhaps have each connector use the high X bits to handle their id. Version means different foreach */
		g_log<DEBUG>("disconnect command received");
		foreach_handlers(msg, [](pair<const uint32_t, unique_ptr<bc::handler> > &p) {
				p.second->disconnect();
			});
	} else {
		g_log<CTRL>("UNKNOWN COMMAND_MSG COMMAND: ", msg->command);
	}
}



void handler::receive_header() {
	/* interpret data as message header and get length, reset remaining */ 
	wrapped_buffer<uint8_t> readbuf = read_queue.extract_buffer();
	const struct message *msg = (const struct message*) readbuf.const_ptr();
	read_queue.to_read(ntoh(msg->length));
	if (msg->version != 0) {
		g_log<DEBUG>("Warning: Unsupported version", (int)msg->version);
		
	}
	if (read_queue.to_read() == 0) { /* payload is packed message */
		if (msg->message_type == REGISTER) {
			/* GC who cares */
			uint32_t oldid = regid;
			/* changing id and sending it. */
			regid = nonce_gen32();
			g_log<CTRL>("UNREGISTERING", oldid);
			g_log<CTRL>("REGISTERING", regid);
			uint32_t netorder = hton(regid);
			write_queue.append((uint8_t*)&netorder, sizeof(netorder));
			state |= SEND_MESSAGE;
			g_messages.erase(oldid);
			/* msg->payload should be zero length here */
			/* send back their new user id */
		} else {
			ostringstream oss("Unknown message: ");
			oss << msg;
			g_log<CTRL>(oss.str());
			/* command and bitcoin payload messages always have a payload */
		}
		read_queue.cursor(0);
		read_queue.to_read(sizeof(struct message));

	} else {
		state = (state & SEND_MASK) | RECV_PAYLOAD;
	}
}

static uint32_t g_message_ids = 1;

void handler::receive_payload() {
	wrapped_buffer<uint8_t> readbuf = read_queue.extract_buffer();
	const struct message *msg = (const struct message*) readbuf.const_ptr();

	if (msg->version != 0 && msg->version != 1) {
		g_log<DEBUG>("Warning: unsupported version. Attempting to receive payload");
		
	}

	switch(msg->message_type) {
	case BITCOIN_PACKED_MESSAGE:
		/* GC register message with each connector, map their ids to a single id and send that back */
		{
			const struct bitcoin::packed_message *bc_msg = (struct bitcoin::packed_message *) msg->payload;
			uint32_t netid = 0;
			if (ntoh(msg->length) < sizeof(struct bitcoin::packed_message) || 
			    ntoh(msg->length) != sizeof(struct bitcoin::packed_message) + bc_msg->length) {
				g_log<ERROR>("Attempted to register invalid message");
			} else {
				uint32_t id = g_message_ids++;
				auto pair = g_messages[this->id].insert(make_pair(id, registered_msg(msg)));
				g_log<CTRL>("Registering message ", regid, (struct bitcoin::packed_message *) msg->payload);
				if (pair.second) {
					netid = hton(id);
					g_log<CTRL>("message registered", regid, id);
				} else {
					netid = 0;
					g_log<ERROR>("Duplicate id generated, surprising");
				}
			}
			write_queue.append((uint8_t*)&netid, sizeof(netid));
			state |= SEND_MESSAGE;
		}
		break;
	case COMMAND:
		handle_message_recv((struct command_msg*) msg->payload, msg->version);
		state = (state & SEND_MASK);
		break;
	case CONNECT:
		{
			/* format is remote packed_net_addr, local packed_net_addr */
			/* currently local is ignored, but would be used if we bound to more than one interface */
			struct connect_payload *payload = (struct connect_payload*) msg->payload;
			g_log<CTRL>("Attempting to connect to", payload->remote_addr, "for", regid);

			int fd(-1);
			// TODO: setting local on the client does nothing, but could specify the interface used
			try {
				fd = Socket(AF_INET, SOCK_STREAM, 0);
				fcntl(fd, F_SETFL, O_NONBLOCK);
			} catch (network_error &e) {
				if (fd >= 0) {
					close(fd);
					fd = -1;
				}
				g_log<ERROR>(e.what(), "(command_handler CONNECT)");
			}

			if (fd >= 0) {
				/* yes, it dangles. It schedules itself for cleanup. yech */
				new bc::connect_handler(fd, payload->remote_addr); 
			}
		}
		break;
	default:
		g_log<CTRL>("unknown payload type", regid, msg);
		break;
	}
	read_queue.cursor(0);
	read_queue.to_read(sizeof(struct message));
	state = (state & SEND_MASK) | RECV_HEADER;

}

void handler::do_read(ev::io &watcher, int /* revents */) {
	ssize_t r(1);
	while(r > 0 && read_queue.hungry()) { 
		while (r > 0 && read_queue.hungry()) {
			pair<int,bool> res(read_queue.do_read(watcher.fd));
			r = res.first;
			if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN) { 
				g_log<ERROR>(strerror(errno), "(command_handler)");
				suicide();
				return;
			}

			if (r == 0) { /* got disconnected! */
				/* LOG disconnect */
				g_log<CTRL>("Orderly disconnect", id);
				suicide();
				return;
			}
		}

		if (read_queue.to_read() == 0) {
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
		}
	}
}

void handler::suicide() {

	close(io.fd);
	io.stop();
	io.fd = -1;

	if (g_active_handlers.find(this) != g_active_handlers.end()) {
		g_active_handlers.erase(this);
		g_inactive_handlers.insert(this);
	}
}

void handler::do_write(ev::io &watcher, int /* revents */) {

	ssize_t r(1);
	while (write_queue.to_write() && r > 0) { 
		pair<int,bool> res = write_queue.do_write(watcher.fd);
		r = res.first;
		if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN) { 
			g_log<ERROR>(strerror(errno), "(command_handler)");
			suicide();
			return;
		}
	}

	if (write_queue.to_write() == 0) {
		state &= ~SEND_MASK;
	}
}

void handler::io_cb(ev::io &watcher, int revents) {
	uint32_t old_state = state;

	if ((state & RECV_MASK) && (revents & ev::READ)) {
		do_read(watcher, revents);
	}

	assert(read_queue.to_read() != 0);
	assert(state & RECV_MASK);
         
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


accept_handler::accept_handler(int fd) 
	: io() { 
	io.set<accept_handler, &accept_handler::io_cb>(this);
	io.set(fd, ev::READ);
	io.start();
}

accept_handler::~accept_handler() {
	close(io.fd);
	io.stop();
}

void accept_handler::io_cb(ev::io &watcher, int /* revents */) {
	struct sockaddr addr = {0, {0}};
	socklen_t len(sizeof(addr));
	int client;
	try {
		client = Accept(watcher.fd, &addr, &len);
		fcntl(client, F_SETFL, O_NONBLOCK);
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

	g_active_handlers.insert(new handler(client));
	for(auto it = g_inactive_handlers.begin(); it != g_inactive_handlers.end(); ++it) {
		delete *it;
	}
	g_inactive_handlers.clear();
}

};

