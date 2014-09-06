#include <iostream>
#include <utility>

#include "collector.hpp"

using namespace std;

void collector::append(wrapped_buffer<uint8_t> &&data, size_t len, uint32_t source_id) {
	
	struct sized_buffer p(data, len, source_id);
	for(auto it = queues.begin(); it != queues.end(); ++it) {
		if (it->first->interested((data.const_ptr())[0])) {
			it->second.push_front(p);
			int events = it->first->get_events();
			it->first->set_events( events | ev::WRITE);
		}
	}
}

struct sized_buffer collector::pop(output_cxn::handler *h) {
	struct sized_buffer rv;
	auto it = queues.find(h);
	if (it == queues.end()) {
		cerr << "LOGIC ERROR, tried to pop non-existant id " << hex << h;
	} else {
		if (it->second.size()) {
			rv = it->second.back();
			it->second.pop_back();
		}
	}
	return rv;
}

void collector::add_consumer(output_cxn::handler *h) {
	queues.insert(make_pair(h, 
	                        deque<struct sized_buffer>()));
}

void collector::retire_consumer(output_cxn::handler *h) {
	queues.erase(h);
}
