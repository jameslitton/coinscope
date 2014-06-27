#ifndef BITCOIN_HPP
#define BITCOIN_HPP

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <memory>
#include <vector>

#include <netinet/in.h>

namespace bitcoin {

extern int32_t g_last_block;

const int32_t MAX_VERSION(70002);
const int32_t MIN_VERSION(209);
const uint64_t SERVICES(1); /* corresponds to NODE_NETWORK */

/* all numbers little endian (x86) except for IP and port in
   bitcoin */


/* because command is 12 bytes (...), this packed form could be slow
   to access but mirrors the wire format */
struct packed_message {
	uint32_t magic;
	char command[12];
	uint32_t length;
	uint32_t checksum;
	uint8_t payload[0];
} __attribute__((packed));

struct packed_net_addr {
	uint32_t time;
	uint64_t services;
	union {
		struct {
			union {
				uint8_t bytes[16];
			} as;
		} ipv6;
		struct {
			char padding[12];
			union {
				uint8_t bytes[4];
				uint32_t number;
				struct in_addr in_addr;
			} as;
		} ipv4;
	} addr;
	uint16_t port;
} __attribute__((packed));

struct packed_version_prefix {
	uint32_t version;
	uint64_t services;
	int64_t timestamp;
	packed_net_addr recv;
	packed_net_addr from;
	uint64_t nonce;
	char user_agent[0]; /* why a variable length variable here!! */
} __attribute__((packed));


struct packed_version_suffix {
	int32_t start_height;
	bool relay;
} __attribute__((packed));

struct combined_version { /* the stupid hurts so bad */
	/* in practice prefix and suffix should be in contiguous memory
	   from the prefix allocation, i.e., prefix should be in wire
	   format to send combined and no need to free suffix */
	size_t size;
	std::unique_ptr<struct packed_version_prefix, void(*)(void*)> prefix;
	struct packed_version_suffix *suffix;


	uint8_t* as_buffer() { 
		return (uint8_t*)prefix.get();
	}

	uint32_t version() const { return prefix->version; }
	uint64_t services() const { return prefix->services; }
	int64_t timestamp() const { return prefix->timestamp; }
	const packed_net_addr * addr_recv() const { return & prefix->recv; }
	const packed_net_addr * addr_from() const { return & prefix->from; }
	uint64_t  nonce() const { return prefix->nonce; }
	char* user_agent() { return prefix->user_agent; }
	int32_t start_height()  const { return suffix->start_height; }
	bool  relay()  const { return suffix->relay; }

	/* can't just have previous methods return reference because of
	   packing, so some strange convoluted-seeming behavior is to get
	   around this restriction */
	void version(uint32_t version)  { prefix->version = version; }
	void services(uint64_t services)  { prefix->services = services; }
	void timestamp(int64_t timestamp)  { prefix->timestamp = timestamp; }
	void addr_recv(const packed_net_addr * x) { 
		memcpy(&prefix->recv, x, sizeof(*x));
	}
	void addr_from(const packed_net_addr * x) { 
		memcpy(&prefix->from, x, sizeof(*x));
	}

	void nonce(uint64_t  nonce)  { prefix->nonce = nonce; }
	void user_agent(const char *s, size_t len) { 
		for(size_t i = 0; i < len; ++i) {
			prefix->user_agent[i] = *s++;
		}
	}
	void start_height(int32_t start)   { suffix->start_height = start; }
	void relay(bool relay)   { suffix->relay = relay; }



	combined_version(size_t agent_length) : 
		size(sizeof(struct packed_version_prefix) + 
		     agent_length + sizeof(struct packed_version_suffix)),
		prefix((struct packed_version_prefix*) malloc(size), free) 
	{
		suffix = (struct packed_version_suffix *)((char *) prefix.get()) + 
			sizeof(struct packed_version_prefix) + agent_length;
	}

};

struct combined_version get_version(const std::string &user_agent,
                                    struct in_addr from, uint16_t from_port,
                                    struct in_addr recv, uint16_t recv_port);

std::unique_ptr<struct packed_message, void(*)(void*)> get_message(const char *command, const uint8_t *payload, size_t len);
std::unique_ptr<struct packed_message, void(*)(void*)> get_message(const char *command, 
                                                                   std::vector<uint8_t> &payload);
inline std::unique_ptr<struct packed_message, void(*)(void*)> get_message(const char *command) { 
	return get_message(command, NULL, 0); 
}

uint32_t compute_checksum(const std::vector<uint8_t> &payload);
uint32_t compute_checksum(const uint8_t *payload, size_t len);

};

#endif
