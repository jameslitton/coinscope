#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <iostream>

#include "network.hpp"
#include "command_structures.hpp"
#include "bitcoin.hpp"

enum log_type {
	INTERNALS,
	CTRL,
	ERROR,
	BITCOIN, /* general status information */
	BITCOIN_MSG, /* actual incoming/outgoing messages */
};


std::ostream & operator<<(std::ostream &o, const struct ctrl::message *m);
std::ostream & operator<<(std::ostream &o, const struct ctrl::message &m);

std::ostream & operator<<(std::ostream &o, const struct bitcoin::packed_message *m);
std::ostream & operator<<(std::ostream &o, const struct bitcoin::packed_message &m);

std::ostream & operator<<(std::ostream &o, const struct sockaddr &addr);

std::string type_to_str(enum log_type type);

class logger { /* just a placeholder to buffer to remote socket (see logserver) */
public:
	std::ostream & operator()(enum log_type type);
	std::ostream & operator()(enum log_type type, uint32_t id);
	std::ostream & operator()(enum log_type type, uint32_t id, bool sender);  /* type has to be BITCOIN_MSG, sender is true if we sent */
};

extern logger g_log;

#endif
