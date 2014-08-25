#ifndef BCWATCH_HPP
#define BCWATCH_HPP

#include <cstdint>
#include <functional>
#include "netinet/in.h"


#include "read_buffer.hpp"

/* just watch the bitcoin channel from the log server, do the endian
   switch, and do callbacks with the new messages */

struct bc_channel_msg {
	uint8_t msg_type; /* should ALWAYS be BITCOIN. Leaving in as a sanity check */
	uint64_t time;
	uint32_t handle_id;
	uint32_t update_type;
	sockaddr_in remote;
	sockaddr_in local;
	uint32_t text_length;
	char text[0];
} __attribute__((packed));

class bcwatch {
public:
	/* this does not take responsibility for closing the stream, though
	   it does consume bytes from it. It does not start until you call
	   start, and then it doesn't yield. It's mostly intended to be
	   used from a separate thread. The callbacks are called from this
	   thread context, so if they run slowly, they will prevent new bc
	   messages from coming through (they'll be queued) */
	bcwatch(int bitcoin_stream, /* stream assumed to be ready to read and blocking */
	        std::function<void(std::unique_ptr<struct bc_channel_msg>)> connect_cb,
	        std::function<void(std::unique_ptr<struct bc_channel_msg>)> disconnect_cb);
	void loop_once(); /* reads the length and the entire message, leaves state ready to read next length */
	void loop_forever() { for(;;) loop_once(); }
	~bcwatch() {};
private:
	int fd;
	std::function<void(std::unique_ptr<struct bc_channel_msg>)> on_connect;
	std::function<void(std::unique_ptr<struct bc_channel_msg>)> on_disconnect;
	read_buffer read_queue;
};

#endif
