#ifndef COLLECTOR_HPP
#define COLLECTOR_HPP

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <deque>

#include "output_cxn.hpp"
#include "wrapped_buffer.hpp"

struct sized_buffer {
	wrapped_buffer<uint8_t> buffer;
	size_t len; // usable length
	uint32_t source_id;
	sized_buffer(wrapped_buffer<uint8_t> other, size_t a_len, uint32_t source) : buffer(other), len(a_len), source_id(source) {}
	sized_buffer(const sized_buffer &o) : buffer(o.buffer), len(o.len), source_id(o.source_id) {}
	sized_buffer() : buffer(), len(0), source_id(0) {}
	sized_buffer & operator=(sized_buffer &o) {
		buffer = o.buffer;
		len = o.len;
		source_id = o.source_id;
		return *this;
	}
};

struct sized_buffer_queue {
	std::deque<struct sized_buffer> queue;
	size_t total_size;
	sized_buffer_queue() : queue(), total_size(0) {};
};

class collector {
public:

	void append(wrapped_buffer<uint8_t> &&data, size_t len, uint32_t source_id);
	struct sized_buffer pop(output_cxn::handler *h);
	void add_consumer(output_cxn::handler *h); /* adds a consumer handler */
	void retire_consumer(output_cxn::handler *h);
	static collector & get() {
		static collector c;
		return c;
	}
private:
	collector() : queues() {}
	std::unordered_map<output_cxn::handler *, sized_buffer_queue> queues;
	                   
};

#endif
