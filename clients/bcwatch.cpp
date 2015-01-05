#include "logger.hpp"
#include "bcwatch.hpp"

#include <unistd.h>
#include <fcntl.h>



using namespace std;

namespace bcwatchers {

static unique_ptr<bc_channel_msg> buf2msg(const read_buffer &read_queue) {
	const struct log_format *log = (const struct log_format*) read_queue.extract_buffer().const_ptr();
	unique_ptr<bc_channel_msg> msg((bc_channel_msg*)::operator new(read_queue.cursor()));
	memcpy(msg.get(), log, read_queue.cursor());
	assert(msg->header.type == BITCOIN);
	msg->header.source_id = ntoh(msg->header.source_id);
	msg->header.timestamp = ntoh(msg->header.timestamp);
	msg->handle_id = ntoh(msg->handle_id);
	msg->update_type = ntoh(msg->update_type);
	msg->text_len = ntoh(msg->text_len);
				
	assert(sizeof(bc_channel_msg) + msg->text_len == read_queue.cursor());
	return msg;
}

ev_handler::ev_handler(int fd,
                       function<void(unique_ptr<bc_channel_msg>)> connect_cb,
                       function<void(unique_ptr<bc_channel_msg>)> disconnect_cb,
                       function<void(const ev_handler *)> fd_loss_cb)
	: read_queue(sizeof(uint32_t)), io(), reading_len(true),
	  on_connect(connect_cb),  on_disconnect(disconnect_cb), on_fd_loss(fd_loss_cb) {
	io.set<ev_handler, &ev_handler::io_cb>(this);
	io.set(fd, ev::READ);
	io.start();
}

void ev_handler::suicide() {
	io.stop();
	close(io.fd);
	io.fd = -1;

	on_fd_loss(this);
}

ev_handler::~ev_handler() {
	if (io.fd >= 0) {
		close(io.fd);
		io.stop();

	}
}

void ev_handler::io_cb(ev::io &watcher, int revents) {
	ssize_t r(1);
	if (revents & ev::READ) {
		while (r > 0 && read_queue.hungry()) {
			pair<int,bool> res(read_queue.do_read(watcher.fd));
			r = res.first;
			if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN) { 
				g_log<ERROR>(strerror(errno), "(bcwatcher)");
				suicide();
				return;
			}

			if (r == 0) { /* got disconnected! */
				/* LOG disconnect */
				g_log<ERROR>("(bcwatcher)");
				suicide();
				return;
			}

			if (read_queue.to_read() == 0) {
				if (reading_len) {
					uint32_t netlen = *((const uint32_t*) read_queue.extract_buffer().const_ptr());
					read_queue.cursor(0);
					read_queue.to_read(ntoh(netlen));
					reading_len = false;
				} else {

					unique_ptr<bc_channel_msg> msg(buf2msg(read_queue));

					if (msg->update_type & (CONNECT_SUCCESS | ACCEPT_SUCCESS)) {
						on_connect(move(msg));
					} else {
						on_disconnect(move(msg));
					}
				                        
					read_queue.cursor(0);
					read_queue.to_read(4);
					reading_len = true;


				}
			}

		}
	}
}



bcwatch::bcwatch(int bitcoin_stream, 
                 function<void(unique_ptr<bc_channel_msg>)> connect_cb,
                 function<void(unique_ptr<bc_channel_msg>)> disconnect_cb) 
	: fd(bitcoin_stream), 
	  on_connect(connect_cb), on_disconnect(disconnect_cb),
	  read_queue(sizeof(uint32_t)) 
{
	assert(!(fcntl(fd, F_GETFL,0) & O_NONBLOCK));
}

void bcwatch::loop_once()  {
	bool reading_len = true;
	for(;;) {
		auto res = read_queue.do_read(fd);
		if (res.first <= 0) {
			throw runtime_error(string("Error reading: ") + strerror(errno));
		}

		/* TODO: put this standard kind of stanza for reading a log in a library */
		if (!read_queue.hungry()) {
			if (reading_len) {
				uint32_t netlen = *((const uint32_t*) read_queue.extract_buffer().const_ptr());
				read_queue.cursor(0);
				read_queue.to_read(ntoh(netlen));
				reading_len = false;
			} else {
				unique_ptr<bc_channel_msg> msg(buf2msg(read_queue));

				if (msg->update_type & (CONNECT_SUCCESS | ACCEPT_SUCCESS)) {
					on_connect(move(msg));
				} else {
					on_disconnect(move(msg));
				}
				                        
				read_queue.cursor(0);
				read_queue.to_read(4);
				reading_len = true;

				return; 
			}
		}

			
	}
	
}

};
