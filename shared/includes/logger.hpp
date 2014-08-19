#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <iostream>
#include <deque>
#include <sstream>

#include "ev++.h"

#include "network.hpp"
#include "command_structures.hpp"
#include "bitcoin.hpp"
#include "write_buffer.hpp"


enum log_type {
	DEBUG=0x2, /* interpret as a string */
	CTRL=0x4, /* control messages */
	ERROR=0x8, /* strings */
	BITCOIN=0x10, /* general status information (strings) */
	BITCOIN_MSG=0x20, /* actual incoming/outgoing messages as encoded */
};


const uint32_t CONNECT_SUCCESS(0x1); // We initiated the connection 
const uint32_t ACCEPT_SUCCESS(0x2); // They initiated the connection (i.e., the result of an accept)
const uint32_t ORDERLY_DISCONNECT(0x4); // Attempt to read returns 0
const uint32_t WRITE_DISCONNECT(0x8); // Attempt to write returns error, disconnected
const uint32_t UNEXPECTED_ERROR(0x10); // We got some kind of other error, indicating potentially iffy state, so disconnected
const uint32_t CONNECT_FAILURE(0x20); // We initiated a connection, but if failed.
const uint32_t PEER_RESET(0x40); // connection reset by peer



std::ostream & operator<<(std::ostream &o, const struct ctrl::message *m);
std::ostream & operator<<(std::ostream &o, const struct ctrl::message &m);

std::ostream & operator<<(std::ostream &o, const struct bitcoin::packed_message *m);
std::ostream & operator<<(std::ostream &o, const struct bitcoin::packed_message &m);

std::ostream & operator<<(std::ostream &o, const struct sockaddr &addr);

inline std::ostream & operator<<(std::ostream &o, const struct sockaddr_in &addr) {
	return o << *(struct sockaddr*)&addr;
}

std::string type_to_str(enum log_type type);


/* all logs preceded by a 32 bit network order length prefix */

/* general log format: */
// uint8_t type
// uint64_t timestamp /* network byte order */
// The rest... (stringstreamed i.e., operator<<(ostream, rest) done)

/* Log format for BITCOIN types */
// uint8_t type (i.e., BITCOIN)
// uint64_t timestamp /* NBO */
// uint32_t id 
// uin32_t update_type //see above
// sockaddr_in remote_addr
// sockaddr_in local_addr
// uint32_t text_len
// char text[text_len]



/* Log format for BITCOIN_MSG types */
//    uint8_t type
//		uint64_t timestamp; /* network byte order */
// 	uint32_t id; /* network byte order */
//		uint8_t is_sender;    
// 	struct packed_message msg
// };


class log_buffer {
public:
	write_buffer write_queue;
	int fd;
	ev::io io;
	/* fd should be a writable unix socket */
	log_buffer(int fd);
	void append(wrapped_buffer<uint8_t> &ptr, size_t len);
	void io_cb(ev::io &watcher, int revents);
	~log_buffer();
};

extern log_buffer *g_log_buffer; /* initialize with log socket and assign */



template <typename T>
void g_log_inner(wrapped_buffer<uint8_t> &wbuf, size_t &len, const T &s) {
	std::stringstream oss;
	oss << s;
	const std::string str(oss.str());
	if (wbuf.allocated() < len + str.size()+1) {
		wbuf.realloc(len + str.size()+1);
	}
	std::copy((uint8_t*)str.c_str(), (uint8_t*)str.c_str() + str.size() + 1, wbuf.ptr() + len);
	len += str.size() + 1;
}

template <typename T, typename... Targs>
void g_log_inner(wrapped_buffer<uint8_t> &wbuf, size_t &len, const T &val, Targs... Fargs) {
	/* if we are in the inners we are in a generic sequence, in which
	   case just make it an ascii stream with a newline at the end */
	std::stringstream oss;
	oss << val << ' ';
	const std::string str(oss.str());
	if (wbuf.allocated() < len + str.size()) {
		wbuf.realloc(len + str.size());
	}
	std::copy((uint8_t*)str.c_str(), (uint8_t*)str.c_str() + str.size(), wbuf.ptr() + len);
	len += str.size();

	g_log_inner(wbuf, len, Fargs...);
}

template <int N, typename... Targs>
void g_log(const std::string &val, Targs... Fargs) {
	uint64_t net_time = hton((uint64_t)time(NULL));

	wrapped_buffer<uint8_t> wbuf(128);
	uint8_t *ptr = wbuf.ptr();
	uint8_t n = N;
	std::copy((uint8_t*)&n, (uint8_t*)(&n) + 1, ptr);
	ptr += 1;
	
	std::copy((uint8_t*) &net_time, ((uint8_t*)&net_time) + sizeof(net_time),
	          ptr);
	ptr += sizeof(net_time);;

	size_t len = sizeof(net_time) + 1;
	
	g_log_inner(wbuf,len,val, Fargs...);
	if (g_log_buffer) {
		g_log_buffer->append(wbuf, len);
	} else {
		std::cerr << "<<CONSOLE FALLBACK>> " << ((char*) wbuf.const_ptr() + 1 + sizeof(net_time)) << std::endl;
	}
}

template <int N> void g_log(uint32_t id, bool is_sender, const struct bitcoin::packed_message *m);
template <> void g_log<BITCOIN_MSG>(uint32_t id, bool is_sender, const struct bitcoin::packed_message *m);

template <int N> void g_log(uint32_t update_type, uint32_t handle_id, const struct sockaddr_in &remote, 
                            const struct sockaddr_in &local, const char * text, uint32_t text_len);
template <> void g_log<BITCOIN>(uint32_t update_type, uint32_t handle_id, const struct sockaddr_in &remote, 
                                const struct sockaddr_in &local, const char * text, uint32_t text_len);

#endif
