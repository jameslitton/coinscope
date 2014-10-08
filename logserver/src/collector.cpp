#include <iostream>
#include <utility>
#include <set>

#include "config.hpp"
#include "collector.hpp"

using namespace std;

void collector::append(wrapped_buffer<uint8_t> &&data, size_t len, uint32_t source_id) {
	
	static size_t max_size = 0;
	struct sized_buffer p(data, len, source_id);
	if (max_size == 0) {
		const libconfig::Config *cfg(get_config());
		max_size = (uint32_t)cfg->lookup("logger.max_buffer");
	}
	set<output_cxn::handler *> morose;
	for(auto it = queues.begin(); it != queues.end(); ++it) {
		if (it->first->interested((data.const_ptr())[0])) {
			it->second.total_size += p.len;
			it->second.queue.push_front(p);
			int events = it->first->get_events();
			it->first->set_events( events | ev::WRITE);

			if (it->second.total_size > max_size) {
				cerr << "Total size is " << it->second.total_size << endl;
				morose.insert(it->first); /* do this out of band, because upon death they are removed from the queue, invalidating iterators */
			}

		}
	}

	for(auto it = morose.begin(); it != morose.end(); ++it) {
		cerr << "Disconnecting handler due to size exceeded " << max_size << endl;
		delete (*it);
	}
}

struct sized_buffer collector::pop(output_cxn::handler *h) {
	struct sized_buffer rv;
	auto it = queues.find(h);
	if (it == queues.end()) {
		cerr << "LOGIC ERROR, tried to pop non-existant id " << hex << h;
	} else {
		if (it->second.queue.size()) {
			rv = it->second.queue.back();
			it->second.queue.pop_back();
			it->second.total_size -= rv.len;
		}
	}
	return rv;
}

void collector::add_consumer(output_cxn::handler *h) {
	queues.insert(make_pair(h, 
	                        sized_buffer_queue()));
}

void collector::retire_consumer(output_cxn::handler *h) {
	queues.erase(h);
}
