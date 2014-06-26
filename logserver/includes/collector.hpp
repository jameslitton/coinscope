#ifndef COLLECTOR_HPP
#define COLLECTOR_HPP

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <deque>

#include "output_cxn.hpp"
#include "cvector.hpp"


class collector {
public:

	void append(cvector<uint8_t> data);
	std::shared_ptr<cvector<uint8_t> > pop(output_cxn::handler *h);
	void add_consumer(output_cxn::handler *h); /* adds a consumer handler */
	void retire_consumer(output_cxn::handler *h);
	static collector & get() {
		static collector c;
		return c;
	}
private:
	collector() : queues() {}
	std::unordered_map<output_cxn::handler *, 
	                   std::deque<std::shared_ptr<cvector<uint8_t> > > > queues;
};

#endif
