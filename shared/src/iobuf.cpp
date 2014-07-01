#include <cassert>
#include <cstdint>

#include <memory>
#include <iterator>
#include <iostream>

#include "iobuf.hpp"

using namespace std;


/* all event data goes into here. It is FIFO. Must optimize*/
uint8_t * iobuf::offset_buffer() {
	return buffer.begin() + loc;
}

uint8_t * iobuf::raw_buffer() {
	return buffer.data();
}

size_t iobuf::location() const { return loc; }

void iobuf::seek(size_t new_loc) { 
	assert(new_loc <= buffer.size());
	loc = new_loc;
}

cvector<uint8_t> iobuf::extract(size_t k_bytes) {
	cvector<uint8_t> retbuf;
	retbuf.swap(buffer);
	loc = 0;
	retbuf.lazy_resize(k_bytes);
	return move(retbuf);
}

void iobuf::grow(size_t x) {
	if (x > buffer.size()) {
		buffer.lazy_resize(x);
	}
}

void iobuf::shrink(size_t x) {
	if (x < buffer.size()) {
		buffer.lazy_resize(x);
	}
}



namespace iobuf_spec {

void append(iobuf *buf, const uint8_t *ptr, size_t len) {
	buf->grow(buf->location() + len);
	copy(ptr, ptr + len, buf->offset_buffer());
}

};
