#include "connector.hpp"
#include "network.hpp"

using namespace std;

namespace ctrl {
namespace easy {

message::message(enum ctrl::message_types type, const uint8_t *payload, size_t payload_length) 
	: buffer(sizeof(ctrl::message) + payload_length) {
	struct ctrl::message *msg = (struct ctrl::message *) buffer.ptr();
	msg->version = 0;
	msg->length = ntoh((uint32_t)payload_length);
	msg->message_type = type;
	memcpy(msg->payload, payload, payload_length);
}

message::message(enum ctrl::message_types type, const std::vector<uint8_t> &payload, size_t payload_length)
	: buffer(sizeof(ctrl::message) + payload_length) {
	struct ctrl::message *msg = (struct ctrl::message *) buffer.ptr();
	msg->version = 0;
	msg->length = ntoh((uint32_t)payload_length);
	msg->message_type = type;
	memcpy(msg->payload, payload.data(), payload_length);
}

message::message(const wrapped_buffer<uint8_t> &contents) : buffer(contents) {}
message::message(message &&moved) : buffer(moved.buffer) {}
message::message(const message &copy) : buffer(copy.buffer) {}
message & message::operator=(const message &other) {
	buffer = other.buffer;
	return *this;
}

std::pair<wrapped_buffer<uint8_t>, size_t> message::serialize() const {
	const struct ctrl::message *msg = (const struct ctrl::message*) buffer.const_ptr();
	return make_pair(buffer, ntoh(msg->length) + sizeof(*msg));
}

unique_ptr<message> message::deserialize(const wrapped_buffer<uint8_t> &buffer) {
	unique_ptr<message> rv;
	const struct ctrl::message *msg = (const struct ctrl::message*) buffer.const_ptr();
	switch(msg->message_type) {
	case BITCOIN_PACKED_MESSAGE:
		rv = unique_ptr<ctrl::easy::message>(new bitcoin_msg(buffer));
		break;
	case COMMAND:
		rv = unique_ptr<ctrl::easy::message>(new command_msg(buffer));
		break;
	case REGISTER:
		rv = unique_ptr<ctrl::easy::message>(new register_msg(buffer));
		break;
	case CONNECT:
		rv = unique_ptr<ctrl::easy::message>(new connect_msg(buffer));
		break;
	default:
		throw runtime_error("Unknown type");
		break;
	}
	return rv;
}

ctrl::message_types message::type() const {
	const struct ctrl::message *msg = (const struct ctrl::message *) buffer.const_ptr();
	return (ctrl::message_types)msg->message_type;
}

void message::type(enum ctrl::message_types t) {
	struct ctrl::message *msg = (struct ctrl::message *) buffer.ptr();
	msg->message_type = t;
}


const uint8_t * bitcoin_msg::payload() const {
	const struct ctrl::message *msg = (const struct ctrl::message *) buffer.const_ptr();
	return msg->payload;
}

void bitcoin_msg::payload(const uint8_t *new_payload, size_t length) {
	const struct ctrl::message *msg = (const struct ctrl::message *) buffer.const_ptr();
	if (length != ntoh(msg->length)) {
		buffer.realloc(length + sizeof(*msg));
	}
	struct ctrl::message *new_msg = (struct ctrl::message *) buffer.ptr();
	memcpy(new_msg->payload, new_payload, length);
	new_msg->length = ntoh((uint32_t)length);
}


connect_msg::connect_msg(const struct sockaddr_in *remote_addr, const struct sockaddr_in *local_addr) 
	: message(CONNECT, vector<uint8_t>(sizeof(*remote_addr) * 2), sizeof(*remote_addr) * 2) {
	struct ctrl::message *msg = (struct ctrl::message *) buffer.ptr();
	memcpy(msg->payload, remote_addr, sizeof(*remote_addr));
	memcpy(msg->payload + sizeof(*remote_addr), local_addr, sizeof(*local_addr));
}

const struct sockaddr_in * connect_msg::remote_addr() const {
	const struct ctrl::message *msg = (const struct ctrl::message *) buffer.const_ptr();
	return (struct sockaddr_in*) msg->payload;
}

void connect_msg::remote_addr(const struct sockaddr_in *new_remote) {
	struct sockaddr_in *remote = (struct sockaddr_in *) (((struct ctrl::message *) buffer.ptr())->payload);
	memcpy(remote, new_remote, sizeof(*new_remote));
}

const struct sockaddr_in * connect_msg::local_addr() const {
	const struct ctrl::message *msg = (const struct ctrl::message *) buffer.const_ptr();
	return (struct sockaddr_in*) (msg->payload + sizeof(sockaddr_in*));
}

void connect_msg::local_addr(const struct sockaddr_in *new_local) {
	struct sockaddr_in *local = (struct sockaddr_in *) (((struct ctrl::message *) buffer.ptr())->payload + sizeof(sockaddr_in));
	memcpy(local, new_local, sizeof(*new_local));
}


command_msg::command_msg(enum commands a_command, uint32_t a_message_id, const uint32_t *a_targets, size_t a_target_cnt)
	: message(wrapped_buffer<uint8_t>(sizeof(ctrl::message) + sizeof(ctrl::command_msg) + 4*a_target_cnt)) { 
	ctrl::message *msg = (ctrl::message *)buffer.ptr();
	msg->version = 0;
	msg->message_type = COMMAND;
	msg->length = hton((uint32_t)(sizeof(ctrl::command_msg) + 4 * a_target_cnt));
	struct ctrl::command_msg *cmsg = (struct ctrl::command_msg*) msg->payload;
	cmsg->command = a_command;
	cmsg->message_id = hton(a_message_id);
	for(size_t i = 0; i < a_target_cnt; ++i) {
		cmsg->targets[i] = hton(a_targets[i]);
	}
	cmsg->target_cnt = hton((uint32_t)a_target_cnt);
}
command_msg::command_msg(enum commands command, uint32_t message_id /* host byte order */, const std::vector<uint32_t> &targets) 
	: command_msg(command, message_id, targets.data(), targets.size()) {
}

enum commands command_msg::command() const {
	const ctrl::message *msg = (const ctrl::message*) buffer.const_ptr();
	const struct ctrl::command_msg *cmsg = (const struct ctrl::command_msg*) msg->payload;
	return (enum commands)cmsg->command;
}
void command_msg::command(enum commands a_command) {
	ctrl::message *msg = (ctrl::message*) buffer.ptr();
	struct ctrl::command_msg *cmsg = (struct ctrl::command_msg*) msg->payload;
	cmsg->command = a_command;
}

vector<uint32_t> command_msg::targets() const {
	vector<uint32_t> rv;
	const ctrl::message *msg = (const ctrl::message*) buffer.const_ptr();
	const struct ctrl::command_msg *cmsg = (const struct ctrl::command_msg*) msg->payload;
	uint32_t tc = ntoh(cmsg->target_cnt);
	rv.reserve(tc);
	for(uint32_t i = 0; i < tc; ++i) {
		rv.push_back(ntoh(cmsg->targets[i]));
	}
	return rv;
}
void command_msg::targets(const uint32_t *targets, size_t cnt) {
	ctrl::message *msg = (ctrl::message*) buffer.ptr();
	struct ctrl::command_msg *cmsg = (struct ctrl::command_msg*) msg->payload;
	if (cnt == ntoh(cmsg->target_cnt)) { /* can do in place */
		for(size_t i = 0; i < cnt; ++i) {
			cmsg->targets[i] = hton(targets[i]);
		}
	} else {
		command_msg newobj((enum commands)cmsg->command, ntoh(cmsg->message_id), targets, cnt);
		*this = newobj;
	}
}

void command_msg::targets(const std::vector<uint32_t> & targets) {
	this->targets(targets.data(), targets.size());
}

uint32_t command_msg::message_id() const {
	const ctrl::message *msg = (const ctrl::message*) buffer.const_ptr();
	const struct ctrl::command_msg *cmsg = (const struct ctrl::command_msg*) msg->payload;
	return ntoh(cmsg->message_id);
}
void command_msg::message_id(uint32_t message_id) {
	ctrl::message *msg = (ctrl::message*) buffer.ptr();
	struct ctrl::command_msg *cmsg = (struct ctrl::command_msg*) msg->payload;
	cmsg->message_id = hton(message_id);
}

};
};

