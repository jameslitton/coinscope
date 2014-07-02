#ifndef COMMAND_STRUCTURES_HPP
#define COMMAND_STRUCTURES_HPP

/* since this is not bitcoin, integers are sent in network byte order */

#ifdef __cplusplus
namespace ctrl {
#endif

#define CONTROL_PATH "/tmp/bitcoin_control"

enum commands {
	COMMAND_GET_CXN,
	COMMAND_DISCONNECT,
	COMMAND_GETADDR 
};

const uint32_t BROADCAST_TARGET(0xFFFFFFFF);

enum message_types {
	BITCOIN_PACKED_MESSAGE,
	COMMAND,
	REGISTER,
	CONNECT,
};

struct message {
	uint32_t length; /* sizeof(payload) */
	uint32_t message_type;
	uint8_t payload[0];
};

struct register_msg {
	struct message msg; /* returns new id */
};

struct connect_payload { 
	struct in_addr remote_inaddr;
	uint16_t remote_port;
	struct in_addr local_inaddr; /* these should maybe be decided by the connector...*/
	uint16_t local_port;
}__attribute__((packed));



struct command_msg {
	uint32_t version;
	uint32_t command;
	uint32_t message_id;
	/* still have to decide the format of target, as it depends on some
	   data structure changes, but target will correspond to indices in
	   the logs and values returned by COMMAND_GET_CXN */

	uint32_t target_cnt;
	uint32_t targets[0]; 
};

/*
  send_message(length, message) //returns message id
*/







#ifdef __cplusplus
};
#endif

#endif
