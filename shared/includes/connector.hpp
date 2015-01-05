#ifndef CONNECTOR_HPP
#define CONNECTOR_HPP

#include "wrapped_buffer.hpp"
#include "command_structures.hpp"

namespace ctrl {
namespace easy {

/* this just provides an 'easy' API that just wraps the
   traditional/old ctrl interface structures */

class message {
private:
	message();
protected:
	wrapped_buffer<uint8_t> buffer;
public:
	message(enum ctrl::message_types type, const uint8_t *payload, size_t payload_length);
	message(enum ctrl::message_types type, const std::vector<uint8_t> &payload, size_t payload_length);
	message(const wrapped_buffer<uint8_t> &contents); 
	message(message &&moved);
	message(const message &copy);
	message & operator=(const message &other);
	virtual ~message() {};
	virtual std::pair<wrapped_buffer<uint8_t>, size_t> serialize() const;
	static std::unique_ptr<message> deserialize(const wrapped_buffer<uint8_t> &buffer);

	ctrl::message_types type() const;
	void type(enum ctrl::message_types t);
};

class register_msg : public message {
public:
	register_msg() : message(REGISTER, nullptr, 0) {}
	register_msg(const wrapped_buffer<uint8_t> &contents) : message(contents) {}
	register_msg(register_msg &&moved) : message(std::move(moved.buffer)) {}
	register_msg(const register_msg &copy) : message(copy.buffer) {}
	register_msg & operator=(const register_msg &other) {
		buffer = other.buffer;
		return *this;
	}
};

class bitcoin_msg : public message {
public:
	bitcoin_msg(const uint8_t *payload, size_t payload_size) : message(BITCOIN_PACKED_MESSAGE, payload, payload_size) {}
	bitcoin_msg(const std::vector<uint8_t> &payload, size_t payload_size) : message(BITCOIN_PACKED_MESSAGE, payload, payload_size) {}
	bitcoin_msg(const wrapped_buffer<uint8_t> &contents) : message(contents) {}
	bitcoin_msg(bitcoin_msg &&moved) : message(std::move(moved.buffer)) {}
	bitcoin_msg(const bitcoin_msg &copy) : message(copy.buffer) {}
	bitcoin_msg & operator=(const bitcoin_msg &other) {
		buffer = other.buffer;
		return *this;
	}

	const uint8_t * payload() const;
	void payload(const uint8_t *new_payload, size_t length);
};

class connect_msg : public message {
public:
	connect_msg(const struct sockaddr_in *remote_addr, const struct sockaddr_in *local_addr);
	connect_msg(const wrapped_buffer<uint8_t> &contents) : message(contents) {}
	connect_msg(connect_msg &&moved) : message(std::move(moved.buffer)) {}
	connect_msg(const connect_msg &copy) : message(copy.buffer) {}
	connect_msg & operator=(const connect_msg &other) {
		buffer = other.buffer;
		return *this;
	}

	const struct sockaddr_in * remote_addr() const;
	void remote_addr(const struct sockaddr_in *);

	const struct sockaddr_in * local_addr() const;
	void local_addr(const struct sockaddr_in *);
};

class command_msg : public message {
public:
	command_msg(enum commands command, uint32_t message_id /* host byte order */, const std::vector<uint32_t> &targets);
	command_msg(enum commands command, uint32_t message_id, const uint32_t *targets, size_t target_cnt);
	command_msg(const wrapped_buffer<uint8_t> &contents) : message(contents) {}
	command_msg(command_msg &&moved) : message(std::move(moved.buffer)) {}
	command_msg(const command_msg &copy) : message(copy.buffer) {}
	command_msg & operator=(const command_msg &other) {
		buffer = other.buffer;
		return *this;
	}

	enum commands command() const;
	void command(enum commands);

	std::vector<uint32_t> targets() const;
	void targets(const std::vector<uint32_t>&);
	void targets(const uint32_t *targets, size_t cnt);

	uint32_t message_id() const;
	void message_id(uint32_t);
};

};
};
#endif
