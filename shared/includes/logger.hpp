#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <iostream>

#include "network.hpp"
#include "command_structures.hpp"
#include "bitcoin.hpp"


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


/* Log format for BITCOIN_MSG types */
// struct log_format {
//		uint64_t timestamp; /* network byte order */
// 	uint32_t id; /* network byte order */
//		uint8_t is_sender;    
// 	struct packed_message msg
// };

template <typename T>
void g_log_inner(const T &s) {
	std::cout << s << std::endl;
}

template <typename T, typename... Targs>
void g_log_inner(const T &val, Targs... Fargs) {
	std::cout << val << ' ';
	g_log_inner(Fargs...);
}

template <int N, typename... Targs>
void g_log(const std::string &val, Targs... Fargs) {
	std::cout << '[' << time(NULL) << "] " << type_to_str((log_type)N) << ": ";
	g_log_inner(val, Fargs...);
}

template <int N> void g_log(uint32_t id, bool is_sender, const struct bitcoin::packed_message *m);
template <> void g_log<BITCOIN_MSG>(uint32_t id, bool is_sender, const struct bitcoin::packed_message *m);

#endif
