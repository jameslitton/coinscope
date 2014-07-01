#ifndef IOBUF_HPP
#define IOBUF_HPP

#include <memory>

#include "cvector.hpp"

class iobuf;

namespace bitcoin {
struct packed_message;
};

namespace iobuf_spec {

void append(iobuf *, const uint8_t *ptr, size_t len);

template <typename T> void append(iobuf *buf, const T *ptr) {
	append(buf, (const uint8_t*) ptr, sizeof(*ptr));
}
template <> void append<bitcoin::packed_message>(iobuf *buf, const bitcoin::packed_message *ptr);
template <> void append<std::unique_ptr<bitcoin::packed_message, void(*)(void*)> >(iobuf *buf, const std::unique_ptr<bitcoin::packed_message, void(*)(void*)> *ptr);
};

/* all event data goes into here. It is FIFO. Must optimize*/
class iobuf {
public:
	uint8_t * offset_buffer();
	uint8_t * raw_buffer();
	size_t location() const;

	template <typename T>
	void append(const T *ptr) {
		iobuf_spec::append<T>(this, ptr);
	}

	cvector<uint8_t> extract(size_t k_bytes);
	void seek(size_t new_loc);

	void grow(size_t x);
	void shrink(size_t x);
	size_t end() { return buffer.size(); }

	iobuf() : buffer(), loc(0) {}
protected:
	cvector<uint8_t> buffer;
	size_t loc;
};


#endif
