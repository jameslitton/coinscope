#ifndef BITCOIN_HPP
#define BITCOIN_HPP

#include <cstdint>
#include <cstdlib>

#include <memory>

namespace bitcoin {

const int32_t MAX_VERSION(7002);
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
   unsigned char payload[0];
} __attribute__((packed));

struct packed_net_addr {
   uint32_t time;
   uint64_t services;
   char ipv[16];
   uint16_t port;
} __attribute__((packed));

struct packed_version_prefix {
   uint32_t version;
   uint64_t services;
   int64_t timestamp;
   packed_net_addr addr_recv;
   packed_net_addr addr_from;
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
   std::unique_ptr<struct packed_version_prefix, void(*)(void*)> prefix;
   struct packed_version_suffix *suffix;

   /* to simplify use */
   uint32_t & version() { return prefix->version; }
   uint64_t & services() { return prefix->services; }
   int64_t & timestamp() { return prefix->timestamp; }
   packed_net_addr& addr_recv() { return prefix->addr_recv; }
   packed_net_addr& addr_from() { return prefix->addr_from; }
   uint64_t & nonce() { return prefix->nonce; }
   char* user_agent() { return prefix->user_agent; }
   int32_t& start_height()  { return suffix->start_height; }
   bool & relay()  { return suffix->relay; }

   combined_version(size_t agent_length) : 
      prefix((struct packed_version_prefix*) malloc(sizeof(struct packed_version_prefix) + 
                    agent_length + sizeof(struct packed_version_suffix)), free) {
      suffix = (struct packed_version_suffix *)((char *) prefix.get()) + sizeof(struct packed_version_prefix) + agent_length;
   }


};

};

#endif
