#ifndef BCWATCH_HPP
#define BCWATCH_HPP

#include <cstdint>
#include <functional>
#include <ev++.h>
#include "netinet/in.h"


#include "logger.hpp"
#include "read_buffer.hpp"

/* just watch the bitcoin channel from the log server, do the endian
   switch, and do callbacks with the new messages */

typedef struct bitcoin_log_format bc_channel_msg ; /* typedef usage implies in host order */

namespace bcwatchers {

class ev_handler { /* for use with non-blocking IO in a libev loop with g_logging enabled */
public:
	ev_handler(int fd,
	           std::function<void(std::unique_ptr<bc_channel_msg>)> connect_cb,
	           std::function<void(std::unique_ptr<bc_channel_msg>)> disconnect_cb,
	           std::function<void(const ev_handler *)> fd_loss_cb
	           );
	~ev_handler();
	void io_cb(ev::io &watcher, int revents);
private:

	read_buffer read_queue;
	ev::io io;
	bool reading_len;
	std::function<void(std::unique_ptr<bc_channel_msg>)> on_connect;
	std::function<void(std::unique_ptr<bc_channel_msg>)> on_disconnect;
	std::function<void(const ev_handler *)> on_fd_loss;
	void suicide();
	ev_handler & operator=(ev_handler other);
	ev_handler(const ev_handler &);
	ev_handler(const ev_handler &&other);
	ev_handler & operator=(ev_handler &&other);
};


class bcwatch {
public:
	/* this does not take responsibility for closing the stream, though
	   it does consume bytes from it. It does not start until you call
	   start, and then it doesn't yield. It's mostly intended to be
	   used from a separate thread. The callbacks are called from this
	   thread context, so if they run slowly, they will prevent new bc
	   messages from coming through (they'll be queued) */
	bcwatch(int bitcoin_stream, /* stream assumed to be ready to read and blocking */
	        std::function<void(std::unique_ptr<bc_channel_msg>)> connect_cb,
	        std::function<void(std::unique_ptr<bc_channel_msg>)> disconnect_cb);
	void loop_once(); /* reads the length and the entire message, leaves state ready to read next length */
	void loop_forever() { for(;;) loop_once(); }
	~bcwatch() {};
private:
	int fd;
	std::function<void(std::unique_ptr<bc_channel_msg>)> on_connect;
	std::function<void(std::unique_ptr<bc_channel_msg>)> on_disconnect;
	read_buffer read_queue;
};

};

#endif
