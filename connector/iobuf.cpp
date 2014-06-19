#include <cassert>
#include <cstdint>

#include <memory>
#include <iterator>

#include "iobuf.hpp"
#include "bitcoin.hpp"

using namespace std;


/* all event data goes into here. It is FIFO. Must optimize*/
uint8_t * iobuf::offset_buffer() {
	assert(buffer);
	return buffer.get() + loc;
}

uint8_t * iobuf::raw_buffer() {
	assert(buffer);
	return buffer.get();
}

size_t iobuf::location() const { return loc; }

void iobuf::seek(size_t new_loc) { 
	assert(new_loc < allocated);
	loc = new_loc;
}

void iobuf::reserve(size_t x) {
	if (x > allocated) {
		unique_ptr<uint8_t[]> tmp(new uint8_t[x]);
		copy(buffer.get(), buffer.get() + allocated, tmp.get());
		buffer = move(tmp);
		allocated = x;
	}
}

void iobuf::shrink(size_t x) {
	if (x < allocated) {
		unique_ptr<uint8_t[]> tmp(new uint8_t[x]);
		copy(buffer.get(), buffer.get() + x, tmp.get());
		buffer = move(tmp);
		allocated = x;
	}
}



namespace iobuf_spec {

void append(iobuf *buf, const uint8_t *ptr, size_t len) {
	buf->reserve(buf->location() + len);
	copy(ptr, ptr + sizeof(*ptr), buf->offset_buffer());
	buf->seek(buf->location() + len);
}

template <> void append<bitcoin::packed_message>(iobuf *buf, const bitcoin::packed_message *ptr) {
	append(buf, (const uint8_t*)ptr, sizeof(*ptr) + ptr->length);
}

};
