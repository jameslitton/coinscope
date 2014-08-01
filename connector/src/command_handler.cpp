#include <iostream>
#include <iterator>
#include <sstream>
#include <map>

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

uint32_t handler::id_pool = 0;


class registered_msg {
public:
	time_t registration_time;
	shared_ptr<struct bitcoin::packed_message> msg;

	registered_msg(time_t regtime, const struct message *messg) 
		: registration_time(regtime), 
		  msg((struct bitcoin::packed_message *) ::operator new(ntoh(messg->length)))
	{
		memcpy(msg.get(), &messg->payload, ntoh(messg->length));
	}
	registered_msg(registered_msg &&other) 
		: registration_time(other.registration_time),
		  msg(move(other.msg)) {}

	shared_ptr<struct bitcoin::packed_message> get_buffer() { return msg; }

};

map<uint32_t, registered_msg> g_messages;


void handler::handle_message_recv(const struct command_msg *msg) { 
	vector<uint8_t> out;

	if (msg->command == COMMAND_GET_CXN) {
		g_log<CTRL>("All connections requested", regid);
		/* format is struct connection_info */

		wrapped_buffer<uint8_t> buffer;
		buffer.realloc(bc::g_active_handlers.size() * sizeof(struct connection_info));
		/* I could append these piecemeal to the write_queue, but this would cause more allocations/gc. This does it as one big chunk in the list,
		   which for an active connector should be one mmapped
		   segment */
		uint8_t *writebuf = buffer.ptr();
		for(bc::handler_map::const_iterator it = bc::g_active_handlers.cbegin(); it != bc::g_active_handlers.cend(); ++it) {
			struct connection_info out;
			out.handle_id = hton(it->first);
			out.remote_addr = it->second->get_remote_addr();
			out.local_addr = it->second->get_local_addr();
			memcpy(writebuf, &out, sizeof(out));
			writebuf += sizeof(out);
		}
		write_queue.append(writebuf, bc::g_active_handlers.size() * sizeof(struct connection_info));
		state |= SEND_MESSAGE;
	} else if (msg->command == COMMAND_SEND_MSG) {
		uint32_t message_id = ntoh(msg->message_id);
		auto it = g_messages.find(message_id);
		if (it == g_messages.end()) {
			g_log<ERROR>("invalid message id", message_id);
		} else {
			shared_ptr<struct bc::packed_message> packed(it->second.get_buffer());
			uint32_t target_cnt = ntoh(msg->target_cnt);
			if (target_cnt == 0) {
				for_each(bc::g_active_handlers.begin(), bc::g_active_handlers.end(), [&](pair<uint32_t, bc::handler*> p) {
						p.second->append_for_write(packed.get());
					});
			} else {
				for(uint32_t i = 0; i < target_cnt; ++i) {
					uint32_t target = ntoh(msg->targets[i]);
					bc::handler_map::iterator hit = bc::g_active_handlers.find(target);
					if (hit != bc::g_active_handlers.end()) {
						hit->second->append_for_write(packed.get());
					}
				}
			}
		}
	}
}



void handler::receive_header() {
	/* interpret data as message header and get length, reset remaining */ 
	const struct message *msg = (const struct message*) ((const uint8_t*) read_queue);
	read_queue.to_read(ntoh(msg->length));
	if (msg->version != 0) {
		g_log<DEBUG>("Warning: Unsupported version");
		
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

void handler::receive_payload() {
	const struct message *msg = (const struct message*) ((const uint8_t*)read_queue);

	if (msg->version != 0) {
		g_log<DEBUG>("Warning: unsupported version. Attempting to receive payload");
		
	}

	switch(msg->message_type) {
	case BITCOIN_PACKED_MESSAGE:
		/* register message and send back its id */
		{
			uint32_t id = nonce_gen32();
			auto pair = g_messages.insert(make_pair(id, registered_msg(time(NULL), msg)));
			g_log<CTRL>("Registering message ", regid, (struct bitcoin::packed_message *) msg->payload);
			uint32_t netorder;
			if (pair.second) {
				netorder = hton(id);
				g_log<CTRL>("message registered", regid, id);
			} else {
				netorder = hton((uint32_t)0);
				g_log<ERROR>("Duplicate id generated, surprising");
			}
			write_queue.append((uint8_t*)&netorder, sizeof(netorder));
			state |= SEND_MESSAGE;
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
			try {
				// TODO: setting local on the client does nothing, but could specify the interface used
				fd = Socket(AF_INET, SOCK_STREAM, 0);
				/* This socket is NOT non-blocking. Is this an issue in building up connections? */
				Connect(fd, (struct sockaddr*)&payload->remote_addr, sizeof(payload->remote_addr));
				fcntl(fd, F_SETFL, O_NONBLOCK);
			} catch (network_error &e) {
				g_log<ERROR>(e.what(), "(command_handler)");
			}

			/* send back connect_response */
			struct connect_response response;
			bzero(&response, sizeof(response));

			if (fd >= 0) {
				struct sockaddr_in local;
				socklen_t len = sizeof(local);
				bzero(&local,sizeof(local));
				if (getsockname(fd, (struct sockaddr*) &local, &len) != 0) {
					g_log<ERROR>(strerror(errno));
					memcpy(&local, &payload->local_addr, sizeof(local));
				} 
				bc::handler *h = new bc::handler(fd, bc::SEND_VERSION_INIT, payload->remote_addr, local);
				bc::g_active_handlers.insert(make_pair(h->get_id(), h));

				
				
				response.result = 0;
				response.registration_id = hton(regid);
				response.info.handle_id = hton(h->get_id());
				response.info.remote_addr = payload->remote_addr;
				response.info.local_addr = local;
				
			} else {
				response.result = hton((int32_t)errno);
			}

			write_queue.append((uint8_t*)&response, sizeof(response));
			state |= SEND_MESSAGE;

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
	while(r > 0) { 
		while (r > 0 && read_queue.hungry()) {
			pair<int,bool> res(read_queue.do_read(watcher.fd));
			r = res.first;
			if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN) { 
				g_log<ERROR>(strerror(errno), "(command_handler)");
			}

			if (r == 0) { /* got disconnected! */
				/* LOG disconnect */
				g_log<CTRL>("Orderly disconnect", id);
				io.stop();
				close(io.fd);
				io.fd = -1;
				g_active_handlers.erase(this);
				g_inactive_handlers.insert(this);
				//delete this; This was in the examples, but makes
				//valgrind warn because it still gets referenced by libev
				//before the handler loop completes (my guess is to just
				//remove it from a list). Still, I move it and it gets
				//collected next time someone connects. We'll see if that
				//quiets valgrind.
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
			break;
		}
	}
}

void handler::do_write(ev::io &watcher, int /* revents */) {

	ssize_t r(1);
	while (write_queue.to_write() && r > 0) { 
		pair<int,bool> res = write_queue.do_write(watcher.fd);
		r = res.first;
		if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN) { 
			g_log<ERROR>(strerror(errno), "(command_handler)");
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
	io.stop();
	close(io.fd);
}

void accept_handler::io_cb(ev::io &watcher, int /* revents */) {
	struct sockaddr addr = {0, {0}};
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

	g_active_handlers.insert(new handler(client));
	for(auto it = g_inactive_handlers.begin(); it != g_inactive_handlers.end(); ++it) {
		delete *it;
	}
	g_inactive_handlers.clear();
}

};

