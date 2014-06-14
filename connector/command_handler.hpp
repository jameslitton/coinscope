#ifndef COMMAND_HANDLER_HPP
#define COMMAND_HANDLER_HPP

#include <cstdint>

#include <ev++.h>

#include "command_structures.hpp"
#include "iobuf.hpp"

namespace ctrl {

const uint32_t RECV_MASK = 0x0000ffff; // all receive flags should be in the mask 
const uint32_t RECV_HEADER = 0x1;
const uint32_t RECV_PAYLOAD = 0x2;

const uint32_t SEND_MASK = 0xffff0000;
const uint32_t SEND_MESSAGE = 0x10000;



class handler {
private:
   iobuf read_queue;
   size_t to_read;

   iobuf write_queue; /* most logging should be through logging facility/filter */
   size_t to_write;

   uint32_t state;

public:
	handler() : to_read(sizeof(struct command)),state(RECV_HEADER) {}
   void handle_message_recv(const struct command *msg);
   void io_cb(ev::io &watcher, int revents);
};



#ifdef __cplusplus
};
#endif


#endif
