#include <iostream>
#include <iterator>
#include <sstream>
#include <map>
#include <algorithm>
#include <functional>

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
handler_set g_inactive_handlers;

/* debugging aide */
int32_t g_active_descriptors(0);

uint32_t handler::id_pool = 0;


class registered_msg {
public:
	wrapped_buffer<uint8_t> msg;

	registered_msg(const struct message *messg) 
		: msg(ntoh(messg->length))
	{
		memcpy(msg.ptr(), &messg->payload, ntoh(messg->length));
	}
	registered_msg(registered_msg &&other) 
		: msg(move(other.msg)) {}
	wrapped_buffer<uint8_t> get_buffer() { return msg; }

};

/* TODO, make a vector of registered_messages */
map<uint32_t, map<uint32_t, registered_msg> > g_messages; /* handle_id, register_id, mesg */


handler::~handler() {
	g_messages.erase(id);
	if (io.fd >= 0) {
      --g_active_descriptors;
		close(io.fd);
		io.stop();
	}
}

void foreach_handlers(const struct command_msg *msg, std::function<void(pair<const uint32_t, unique_ptr<bc::handler> >&)> f) {
	uint32_t target_cnt = ntoh(msg->target_cnt);
	if (target_cnt == 1 && msg->targets[0] == BROADCAST_TARGET) {
		for_each(bc::g_active_handlers.begin(), bc::g_active_handlers.end(), f);
	} else {
		if (target_cnt > bc::g_active_handlers.size()) {
			g_log<DEBUG>("Target count larger than all cxn", target_cnt, bc::g_active_handlers.size());
		}
		for(uint32_t i = 0; i < target_cnt; ++i) {
			uint32_t target = ntoh(msg->targets[i]);
			bc::handler_map::iterator hit = bc::g_active_handlers.find(target);
			if (hit != bc::g_active_handlers.end()) {
				f(*hit);
			} else {
				g_log<DEBUG>("Attempting to send command message", ntoh(msg->message_id), "to non-existant target", target);
			}
		}
	}
}

void handler::handle_message_recv(const struct command_msg *msg) { 
	vector<uint8_t> out;

	if (msg->command == COMMAND_GET_CXN) {
		g_log<CTRL>("All connections requested", regid);
		/* format is struct connection_info */

		wrapped_buffer<uint8_t> buffer;
		buffer.realloc(sizeof(uint32_t) + bc::g_active_handlers.size() * sizeof(struct connection_info));
		/* I could append these piecemeal to the write_queue, but this would cause more allocations/gc. This does it as one big chunk in the list,
		   which for an active connector should be one mmapped
		   segment */
		uint8_t *writebuf = buffer.ptr();
		uint32_t len = sizeof(struct connection_info) * bc::g_active_handlers.size();
		len = hton(len);
		memcpy(writebuf, &len, sizeof(len));
		writebuf += sizeof(len);
		for(bc::handler_map::const_iterator it = bc::g_active_handlers.cbegin(); it != bc::g_active_handlers.cend(); ++it) {
			struct connection_info out;
			out.handle_id = hton(it->first);
			out.remote_addr = it->second->get_remote_addr();
			out.local_addr = it->second->get_local_addr();
			memcpy(writebuf, &out, sizeof(out));
			writebuf += sizeof(out);
		}
		write_queue.append(buffer, sizeof(len) + bc::g_active_handlers.size() * sizeof(struct connection_info));
		state |= SEND_MESSAGE;
	} else if (msg->command == COMMAND_SEND_MSG) {
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

	if (msg->version != 0) {
		g_log<DEBUG>("Warning: unsupported version. Attempting to receive payload");
		
	}

	switch(msg->message_type) {
	case BITCOIN_PACKED_MESSAGE:
		/* register message and send back its id */
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
		handle_message_recv((struct command_msg*) msg->payload);
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

   --g_active_descriptors;
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
	int client(-1);;
	try {
		client = Accept(watcher.fd, &addr, &len);
      ++g_active_descriptors;
		fcntl(client, F_SETFL, O_NONBLOCK);
	} catch (network_error &e) {
		if (e.error_code() != EWOULDBLOCK && e.error_code() != EAGAIN && e.error_code() != ECONNABORTED && e.error_code() != EINTR) {
			g_log<ERROR>(e.what(), "Number active handlers: ", g_active_descriptors, " (command_handler)");
         if (client >= 0) {
            --g_active_descriptors;
            close(client);
         }
			/*
			  TODO: Put in a good recovery policy here 
			  watcher.stop();
			  close(watcher.fd);
			  delete this;
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

