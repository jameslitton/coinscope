#include "read_buffer.hpp"

#include <cstring>
#include <stdexcept>

#include <sys/socket.h>

using namespace std;

pair<int,bool> read_buffer::do_read(int fd, size_t size) {
	pair<int,bool> rv;
	ssize_t recv_ret = 0;
	if (size > to_read_) {
		throw invalid_argument("size too large for do_read");
	}
	buffer_.realloc(cursor_ + size);
	assert(buffer_.allocated() >= cursor_ + size);

	if (rb_size_ <= rb_loc_) { /* have to do a recv */
		/* location at size, so can reset */
		rb_size_ = rb_loc_ = 0;
		recv_ret = recv(fd, recv_buffer_.get(), 4096, 0);
		if (recv_ret > 0) {
			rb_size_ += recv_ret;
		}
	}

	if (rb_size_ <= rb_loc_) {
		/* our recv must have failed, propagate error back up */
		rv.first = recv_ret;
	} else {
		size_t rd = min(size, rb_size_ - rb_loc_);
		memcpy(buffer_.ptr() + cursor_, recv_buffer_.get() + rb_loc_, rd);
		rb_loc_ += rd;
		rv.first = rd;
	}

	if (rv.first > 0) {
		cursor_ += rv.first;
		to_read_ -= rv.first;
	} 
	rv.second = to_read_ == 0;
	return rv;
}

pair<int,bool> read_buffer::do_read(int fd) {
	return do_read(fd, to_read_);
}

void read_buffer::to_read(size_t x) {
	to_read_ = x;
}
size_t read_buffer::to_read() const {
	return to_read_;
}

void read_buffer::cursor(size_t loc) {
	assert(loc < buffer_.allocated());
	cursor_ = loc;
}

size_t read_buffer::cursor() const {
	return cursor_;
}

bool read_buffer::hungry() const { return to_read() > 0; }

wrapped_buffer<uint8_t> read_buffer::extract_buffer() {
	return buffer_;
}

const wrapped_buffer<uint8_t> read_buffer::extract_buffer() const {
	return buffer_;
}
