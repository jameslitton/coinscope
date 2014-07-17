#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <iostream>
#include <deque>
#include <sstream>

#include "ev++.h"

#include "network.hpp"
#include "command_structures.hpp"
#include "bitcoin.hpp"
#include "cvector.hpp"
#include "iobuf.hpp"


enum log_type {
	DEBUG=0x2, /* interpret as a string */
	CTRL=0x4, /* control messages */
	ERROR=0x8, /* strings */
	BITCOIN=0x10, /* general status information (strings) */
	BITCOIN_MSG=0x12, /* actual incoming/outgoing messages as encoded */
};


std::ostream & operator<<(std::ostream &o, const struct ctrl::message *m);
std::ostream & operator<<(std::ostream &o, const struct ctrl::message &m);

std::ostream & operator<<(std::ostream &o, const struct bitcoin::packed_message *m);
std::ostream & operator<<(std::ostream &o, const struct bitcoin::packed_message &m);

std::ostream & operator<<(std::ostream &o, const struct sockaddr &addr);

std::string type_to_str(enum log_type type);


/* all logs preceded by a 32 bit network order length prefix */

/* general log format: */
// uint8_t type
// uint32_t timestamp /* network byte order */
// The rest... (stringstreamed i.e., operator<<(ostream, rest) done)

/* Log format for BITCOIN_MSG types */
//    uint8_t type
//		uint64_t timestamp; /* network byte order */
// 	uint32_t id; /* network byte order */
//		uint8_t is_sender;    
// 	struct packed_message msg
// };


class log_buffer {
public:
	iobuf write_queue;
	size_t to_write;
	int fd;
	ev::io io;
	/* fd should be a writable unix socket */
	log_buffer(int fd);
	void append(cvector<uint8_t> &&ptr);
	void io_cb(ev::io &watcher, int revents);
	~log_buffer();
};

extern log_buffer *g_log_buffer; /* initialize with log socket and assign */



template <typename T>
void g_log_inner(cvector<uint8_t> &ptr, const T &s) {
	std::stringstream oss;
	oss << s;
	const std::string str(oss.str());
	std::copy((uint8_t*)str.c_str(), (uint8_t*)str.c_str() + str.size() + 1, std::back_inserter(ptr));
}

template <typename T, typename... Targs>
void g_log_inner(cvector<uint8_t> &ptr, const T &val, Targs... Fargs) {
	/* if we are in the inners we are in a generic sequence, in which
	   case just make it an ascii stream with a newline at the end */
	std::stringstream oss;
	oss << val << ' ';
	const std::string str(oss.str());
	std::copy((uint8_t*)str.c_str(), (uint8_t*)str.c_str() + str.size(), std::back_inserter(ptr));

	g_log_inner(ptr, Fargs...);
}

template <int N, typename... Targs>
void g_log(const std::string &val, Targs... Fargs) {
	uint64_t net_time = hton((uint64_t)time(NULL));
	cvector<uint8_t> ptr(128);
	ptr.push_back(N);
	auto back = std::back_inserter(ptr);
	std::copy((uint8_t*) &net_time, ((uint8_t*)&net_time) + sizeof(net_time),
	     back);
	
	g_log_inner(ptr, val, Fargs...);
	if (g_log_buffer) {
		g_log_buffer->append(move(ptr));
	} else {
		std::cerr << "<<CONSOLE FALLBACK>> " << ((char*) ptr.data() + 1) << std::endl;
	}
}

template <int N> void g_log(uint32_t id, bool is_sender, const struct bitcoin::packed_message *m);
template <> void g_log<BITCOIN_MSG>(uint32_t id, bool is_sender, const struct bitcoin::packed_message *m);

#endif
