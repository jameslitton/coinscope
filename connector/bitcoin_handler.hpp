#ifndef BITCOIN_HANDLER_HPP
#define BITCOIN_HANDLER_HPP

#include <cstdint>

#include <string>

#include <ev++.h>

#include "iobuf.hpp"

namespace bitcoin {


const uint32_t RECV_MASK = 0x0000ffff; // all receive flags should be in the mask 
const uint32_t RECV_HEADER = 0x1;
const uint32_t RECV_PAYLOAD = 0x2;
const uint32_t RECV_VERSION_INIT = 0x4; /* we initiated the handshake */
const uint32_t RECV_VERSION_REPLY = 0x8;

const uint32_t SEND_MASK = 0xffff0000;
const uint32_t SEND_MESSAGE = 0x10000;
const uint32_t SEND_VERSION_INIT = 0x20000; /* we initiated the handshake */
const uint32_t SEND_VERSION_REPLY = 0x40000;
   

const std::string USER_AGENT("specify version string");


class handler {
private:
	iobuf read_queue; /* application needs to read and act on this data */
	size_t to_read;

	iobuf write_queue; /* application wants this written out across network */
	size_t to_write;

	in_addr remote_addr;
	uint16_t remote_port;

	in_addr this_addr; /* address we connected on */
	uint16_t this_port;

	uint32_t state;

public:
	handler(uint32_t a_state = SEND_VERSION_INIT) : state(a_state) {};

	void handle_message_recv(const struct packed_message *msg);
	void io_cb(ev::io &watcher, int revents);

};



};

#endif
