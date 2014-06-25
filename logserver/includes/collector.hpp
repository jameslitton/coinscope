#ifndef COLLECTOR_HPP
#define COLLECTOR_HPP

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <deque>

#include "cvector.hpp"


class collector {
public:

	void append(cvector<uint8_t> data);
	std::shared_ptr<cvector<uint8_t> > pop(uint32_t id);
	uint32_t add_consumer(); /* adds a consumer and returns an id. */
	void retire_consumer(uint32_t);
	static collector & get() {
		static collector c;
		return c;
	}
private:
	collector() {}
	uint32_t idpool;
	std::unordered_map<uint32_t, 
	                   std::deque<std::shared_ptr<cvector<uint8_t> > > > queues;
};

#endif
