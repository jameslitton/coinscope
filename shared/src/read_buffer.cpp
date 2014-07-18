#include "read_buffer.hpp"

#include <stdexcept>

#include <sys/socket.h>


using namespace std;

pair<int,bool> read_buffer::do_read(int fd, size_t size) {
	pair<int,bool> rv;
	if (size > to_read_) {
		throw invalid_argument("size too large for do_read");
	}
	buffer_.realloc(cursor_ + size);
	rv.first = recv(fd, buffer_.ptr() + cursor_, size, 0);
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

mmap_buffer<uint8_t> read_buffer::extract_buffer() {
	return buffer_;
}
