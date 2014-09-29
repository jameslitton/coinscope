#include "write_buffer.hpp"

#include <utility>

#include <unistd.h>

using namespace std;


pair<int,bool> write_buffer::do_write(int fd) {
	return do_write(fd, to_write_);
}
pair<int,bool> write_buffer::do_write(int fd, size_t size) {
	pair<int,bool> rv;

	assert(size);
	assert(buffers_.size());

	/* try to write out the remainder of the page(s) */
	struct buffer_container &curbuf = buffers_.front();

	size_t min_write = min(to_write_, curbuf.writable - curbuf.cursor);
	rv.first = write(fd, curbuf.buffer.const_ptr() + curbuf.cursor, min_write);

	if (rv.first > 0) {
		curbuf.cursor += rv.first;
		to_write_ -= rv.first;
	}

	if (curbuf.cursor == curbuf.writable) {
		buffers_.pop_front();
	}

	assert(to_write_ == 0 || buffers_.size());
	rv.second = to_write_ == 0;
	return rv;
}

void write_buffer::append(const uint8_t *ptr, size_t len) {
	buffers_.emplace_back(ptr, len);
	to_write_ += len;
}
void write_buffer::append(wrapped_buffer<uint8_t> &buf, size_t len) {
	assert(buf.allocated() >= len);
	buffers_.emplace_back(buf, len);
	to_write_ += len;
}

size_t write_buffer::to_write() const { 
	assert(to_write_ == 0 || buffers_.size());
	return to_write_;
}



