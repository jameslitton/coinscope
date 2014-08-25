#include "logger.hpp"
#include "bcwatch.hpp"

#include <unistd.h>
#include <fcntl.h>




using namespace std;

bcwatch::bcwatch(int bitcoin_stream, 
                 function<void(unique_ptr<struct bc_channel_msg>)> connect_cb,
                 function<void(unique_ptr<struct bc_channel_msg>)> disconnect_cb) 
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

				const uint8_t *buf = read_queue.extract_buffer().const_ptr();
				unique_ptr<struct bc_channel_msg> msg((struct bc_channel_msg*)::operator new(read_queue.cursor()));
				memcpy(msg.get(), buf, read_queue.cursor());

				assert(msg->msg_type == BITCOIN);
				msg->time = ntoh(msg->time);
				msg->handle_id = ntoh(msg->handle_id);
				msg->update_type = ntoh(msg->update_type);
				msg->text_length = ntoh(msg->text_length);
				
				assert(sizeof(struct bc_channel_msg) + msg->text_length == read_queue.cursor());

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
