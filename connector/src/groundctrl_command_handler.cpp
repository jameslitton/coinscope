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

	switch(msg->command) {
	case COMMAND_GET_CXN:
		{
			g_log<GROUND>("All connections requested from GC", id);
			/* format is struct connection_info or struct connection_info_v1 */

			/* call each connector command_get_cxn and pack in source_id, get the results (async?), and send back the results */

			easy::command_msg msg(COMMAND_GET_CXN, 0, nullptr, 0);
			std::pair<wrapped_buffer<uint8_t>, size_t> contents = msg.serialize();
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

		}
		break;
	case COMMAND_SEND_MSG:
		{
			/* GC for each connector, map message_id to connector message_id and relay transformed message. Version means use a different foreach_handlers function */
			uint32_t message_id = ntoh(msg->message_id);

			struct easy::command_msg cmsg(COMMAND_SEND_MSG, 0, nullptr, 0);
			map<uint32_t , vector<uint32_t> > targs; /* instance_id -> target_id (host order) */
			uint32_t target_cnt(ntoh(msg->target_cnt));
			for(uint32_t i = 0; i < target_cnt; ++i) {
				uint32_t target(ntoh(msg->targets[i]));
				uint32_t instance = target >> (32-TOM_FD_BITS);
				targs[instance].push_back(target);
			}

			for(auto &p : g_childmap) {
				auto &idx = p.first;
				auto &child = p.second;
				auto it = child.messages.find(message_id);
				if (it == child.messages.end()) {
					g_log<ERROR>("Invalid message id mapping for pid ", child.get_pid(), " with id ", message_id);
				} else {
					cmsg.message_id(it->second);
					cmsg.targets(targs[idx]);
					pair<wrapped_buffer<uint8_t>, size_t> s(cmsg.serialize());
					child.send(s.first, s.second);
				}

			}
			
		}
		break;
	case COMMAND_DELETE_MSG:
		{
			uint32_t message_id = ntoh(msg->message_id);
			struct easy::command_msg cmsg(COMMAND_DELETE_MSG, 0, nullptr, 0);
			for(auto &p : g_childmap) {
				auto &child = p.second;
				auto it = child.messages.find(message_id);
				if (it == child.messages.end()) {
					g_log<ERROR>("Message id ", message_id, " does not have a corresponding mid on connector with pid ", child.get_pid());
				} else {
					cmsg.message_id(it->second);
					pair<wrapped_buffer<uint8_t>, size_t> pkg = cmsg.serialize();
					child.send(pkg.first, pkg.second);
					child.messages.erase(message_id);
				}

			}
		}
		break;
	case COMMAND_DISCONNECT:
		{
			uint32_t target_cnt(ntoh(msg->target_cnt));
			if (target_cnt == 1 && msg->targets[0] == BROADCAST_TARGET) {
				easy::command_msg cmsg(COMMAND_DISCONNECT, 0, msg->targets, 1);
				pair<wrapped_buffer<uint8_t>, size_t> s(cmsg.serialize());				
				for(auto &p : g_childmap) {
					auto &child = p.second;
					child.send(s.first, s.second);
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
		}
		break;
	default:
		g_log<CTRL>("UNKNOWN COMMAND_MSG COMMAND: ", msg->command);
		break;
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

			/* The point of this is to erase any messages that this
			   particular client created because it rejogs its handle_id
			   and each connector. This is sort of a side-effect result
			   and is kind of misnamed and a misfeature. For bc this
			   front-end has to change the handle_id, send the new_id,
			   and effectively (somehow or another) cause the messages
			   related to the old id to be cleared */

			g_log<DEBUG>("REGISTER issued to GC. This is iffy/deprecated");

			g_active_handlers.erase(this);
			uint32_t oldid = id;
			/* changing id and sending it. */

			g_log<CTRL>("UNREGISTERING", oldid);
			g_log<CTRL>("REGISTERING", id);

			uint32_t netorder = hton(id);
			write_queue.append((uint8_t*)&netorder, sizeof(netorder));
			state |= SEND_MESSAGE;

			id = id_pool++;

			g_active_handlers.insert(this);

			struct easy::command_msg cmsg(COMMAND_DELETE_MSG, 0, nullptr, 0);
			for(auto &p : g_childmap) {
				auto &child = p.second;
				for(auto &mp : child.messages) {
					cmsg.message_id(mp.second);
					pair<wrapped_buffer<uint8_t>, size_t> pkg = cmsg.serialize();
					child.send(pkg.first, pkg.second);
				}
				child.messages.clear();
			}

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
			uint32_t local_message_id = g_message_ids++;
					
			for(auto &p : g_childmap) {
				auto &child = p.second;
				child.send(msg, sizeof(*msg) + ntoh(msg->length));
				wrapped_buffer<uint8_t> buf(child.recv(4));
				uint32_t message_id = ntoh( * ((uint32_t*) buf.const_ptr()));
				child.messages[local_message_id] = message_id;
			}

			uint32_t netid(hton(local_message_id));
			write_queue.append((uint8_t*)&netid, sizeof(netid));
			state |= SEND_MESSAGE;
		}

		break;
	case COMMAND:
		handle_message_recv((struct command_msg*) msg->payload/*, msg->version */);
		state = (state & SEND_MASK);
		break;
	case CONNECT:
		{
			/* TODO: have this more smartly load balance. For now, just cycle through */
			auto it = g_childmap.begin();
			advance(it, rand() % g_childmap.size());
			it->second.send(msg, sizeof(*msg) + ntoh(msg->length));

		}
		break;
	default:
		g_log<CTRL>("unknown payload type ", id, msg);
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

