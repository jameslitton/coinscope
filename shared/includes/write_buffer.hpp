#ifndef WRITE_BUFFER_HPP
#define WRITE_BUFFER_HPP

#include <list>
#include <memory>
#include <cstring>

#include "wrapped_buffer.hpp"

/* to be used for accumulating data to be written. */
class write_buffer { 
public:

	/* return value from write, whether the write is complete */
	std::pair<int,bool> do_write(int fd); /* will write to_write_ bytes */
	std::pair<int,bool> do_write(int fd, size_t size); /* will write size bytes */

	void append(const uint8_t *ptr, size_t len);
	void append(wrapped_buffer<uint8_t> &buf, size_t len);

   size_t to_write() const;

	write_buffer() : to_write_(0), buffers_() {}
	~write_buffer() {}

private:

	struct buffer_container {
		size_t cursor; /* location from which we've already written bytes */
		size_t writable; /* first _writable_ bytes in buffer are valid to write */
		wrapped_buffer<uint8_t> buffer;
		buffer_container(wrapped_buffer<uint8_t> b, size_t len) : cursor(0), writable(len), buffer(b) {
			assert(buffer.allocated() >= len);
		}


		/* TODO: make smarter */
		buffer_container(const uint8_t *ptr, size_t len) : cursor(0), writable(len), buffer(len) {
			memcpy(buffer.ptr(), ptr, len);
		}
	};

	size_t to_write_; /* not actually necessary, more a debugging aid */
	std::list<struct buffer_container> buffers_; /* first is bytes in the buffer writable */

};

#endif

