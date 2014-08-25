#ifndef COMMAND_STRUCTURES_HPP
#define COMMAND_STRUCTURES_HPP

/* because of the inclusion of this file, which is not designed to be
   C clean, this file is now no longer C clean. If it is necessary to
   make it work with C let me know. */

#include "bitcoin.hpp"

/* since this is not bitcoin, integers are sent in network byte order */

namespace ctrl {

enum commands { 
	COMMAND_GET_CXN = 1,
	COMMAND_DISCONNECT = 2,
	COMMAND_SEND_MSG = 3
};

const uint32_t BROADCAST_TARGET(0xFFFFFFFF);

enum message_types {
	BITCOIN_PACKED_MESSAGE = 1,
	COMMAND = 2,
	REGISTER= 3,
	CONNECT = 4,
};

struct message {
	uint8_t version; 
	uint32_t length; /* sizeof(payload) */
	uint8_t message_type;
	uint8_t payload[0];
} __attribute__((packed));

/* register_msg returns new id */
typedef message register_msg; 

struct connect_payload { 
	struct sockaddr_in remote_addr;
	struct sockaddr_in local_addr;  /* see comment in command_handler. setting this currently does nothing */
}__attribute__((packed));

struct connection_info { /* response to COMMAND_GET_CXN && part of response for CONNECT command */
	uint32_t handle_id;
	struct sockaddr_in remote_addr;
	struct sockaddr_in local_addr;
} __attribute__((packed));


struct command_msg {
	uint8_t command;
	uint32_t message_id; /* network byte order */
	/* still have to decide the format of target, as it depends on some
	   data structure changes, but target will correspond to indices in
	   the logs and values returned by COMMAND_GET_CXN */

	uint32_t target_cnt; /* network byte order */
	uint32_t targets[0]; /* network byte order */
} __attribute__((packed));

/*
  send_message(length, message) //returns message id
*/


};

#endif
