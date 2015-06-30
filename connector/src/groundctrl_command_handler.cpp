#include <iostream>
#include <iterator>
#include <sstream>
#include <map>
#include <algorithm>

#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "command_handler.hpp"
#include "connector.hpp"
#include "netwrap.hpp"
#include "network.hpp"
#include "logger.hpp"
#include "child_handler.hpp"
#include "main.hpp"

using namespace std;

namespace bc = bitcoin;

extern map<int, child_handler> g_childmap; /* idx -> child_handler */

namespace ctrl {


handler_set g_active_handlers;
handler_set g_inactive_handlers;


/* right now this does particular commands synchronously. Most
 * connector commands are already asynchronous by design, so this
 * primarily affects a few like COMMAND_GET_CXN.  */


uint32_t handler::id_pool = 0;


handler::~handler() {
	if (io.fd >= 0) {
		close(io.fd);
		io.stop();
	}
}

void handler::handle_message_recv(const struct command_msg *msg) { 
	vector<uint8_t> out;

	if (msg->command == COMMAND_GET_CXN) {
		g_log<GROUND>("All connections requested from GC", regid);
		/* format is struct connection_info or struct connection_info_v1 */

		/* call each connector command_get_cxn and pack in source_id, get the results (async?), and send back the results */

		easy::command_msg msg(COMMAND_GET_CXN, 0, nullptr, 0);
		std::pair<wrapped_buffer<uint8_t>, size_t> contents = msg.serialize();
		map<int, child_handler> g_childmap; /* idx -> child_handler */
		vector<std::pair< wrapped_buffer<uint8_t>, size_t > >buffers;
		uint32_t total = 0;
		for(auto &p : g_childmap) {
			auto & child = p.second;
			child.send(contents.first, contents.second);
			uint32_t len;
			wrapped_buffer<uint8_t> buf(child.recv(sizeof(len)));
			memcpy((uint8_t*) &len, buf.const_ptr(), sizeof(len));
			len = ntoh(len);
			buffers.emplace_back(child.recv(len), len);
			total += len;
		}

		wrapped_buffer<uint8_t> buffer(sizeof(total));
		total = hton(total);
		memcpy((uint8_t*) buffer.ptr(), &total, sizeof(total));
		write_queue.append(buffer, sizeof(total));

		for(auto it : buffers) {
			write_queue.append(it.first, it.second);
		}

		state |= SEND_MESSAGE;

	} else if (msg->command == COMMAND_SEND_MSG) {
		/* GC for each connector, map message_id to connector message_id and relay transformed message. Version means use a different foreach_handlers function */
		uint32_t message_id = ntoh(msg->message_id);

		/* maybe just make this non-const...*/
		wrapped_buffer<uint8_t> buffer(sizeof(*msg) + 4*ntoh(msg->target_cnt));
		command_msg *cmsg = (command_msg*) buffer.ptr();
		memcpy(cmsg, msg, sizeof(*msg) + 4*ntoh(msg->target_cnt));

		for(auto &p : g_childmap) {
			auto & child = p.second;
			auto it = child.messages.find(message_id);
			if (it == child.messages.end()) {
				/* these don't have to be instance based, since we will
				 * always always only have one connection per child */
				g_log<ERROR>("invalid message id ", message_id, " for child ", p.first);
			}
			cmsg->message_id = it->second;
			child.send(cmsg, sizeof(cmsg) + 4 * ntoh(cmsg->target_cnt));
		}


	} else if (msg->command == COMMAND_DISCONNECT) {
		uint32_t target_cnt(ntoh(msg->target_cnt));
		if (target_cnt == 1 && msg->targets[0] == BROADCAST_TARGET) {
			for(auto &p : g_childmap) {
				auto &child = p.second;
				child.send(msg, sizeof(*msg) + 4 * target_cnt);
			}
		} else {
			map<uint32_t , vector<uint32_t> > targs; /* instance_id -> target_id (host order) */
			for(uint32_t i = 0; i < target_cnt; ++i) {
				uint32_t target(ntoh(msg->targets[i]));
				uint32_t instance = target >> (32-TOM_FD_BITS);
				targs[instance].push_back(target);
			}
			for(auto &p : g_childmap) {
				auto &idx = p.first;
				auto &child = p.second;
				easy::command_msg cmsg(COMMAND_DISCONNECT, 0, targs[idx]);
				pair<wrapped_buffer<uint8_t>, size_t> s(cmsg.serialize());
				child.send(s.first, s.second);
			}
		}
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
#if 0
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
#endif
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
#if 0
			const struct bitcoin::packed_message *bc_msg = (struct bitcoin::packed_message *) msg->payload;
			uint32_t netid = 0;
			if (ntoh(msg->length) < sizeof(struct bitcoin::packed_message) || 
			    ntoh(msg->length) != sizeof(struct bitcoin::packed_message) + bc_msg->length) {
				g_log<ERROR>("Attempted to register invalid message");
			} else {
				uint32_t id = g_message_ids++;
				//auto pair = g_messages[this->id].insert(make_pair(id, registered_msg(msg)));
				auto pair = g_messages[this->id].insert(make_pair(id, 5));
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
#endif
		}
		break;
	case COMMAND:
		handle_message_recv((struct command_msg*) msg->payload/*, msg->version */);
		state = (state & SEND_MASK);
		break;
	case CONNECT:
		{
			/* format is remote packed_net_addr, local packed_net_addr */
			/* currently local is ignored, but would be used if we bound to more than one interface */
			struct connect_payload *payload = (struct connect_payload*) msg->payload;
			g_log<CTRL>("Attempting to connect to", payload->remote_addr, "for", regid);

			throw "hell noes";

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
				//new bc::connect_handler(fd, payload->remote_addr); 
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

