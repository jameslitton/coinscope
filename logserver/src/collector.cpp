#include <iostream>
#include <utility>

#include "collector.hpp"

using namespace std;

void collector::append(unique_ptr<uint8_t[]> data, size_t len) {
	shared_ptr<collector::wrapped_data> w(new collector::wrapped_data(move(data), len));
	for(auto it = queues.begin(); it != queues.end(); ++it) {
		it->second.push_front(w);
		/* set all clients to watch for writeable events! */
	}
}

shared_ptr<collector::wrapped_data> collector::pop(uint32_t id) {
	shared_ptr<collector::wrapped_data> rv;
	auto it = queues.find(id);
	if (it == queues.end()) {
		cerr << "LOGIC ERROR, tried to pop non-existant id " << id;
		/* TODO: HANDLE LOGIC ERROR */
	} else {
		if (it->second.size()) {
			rv = it->second.back();
			it->second.pop_back();
		}
	}
	return rv;
}

uint32_t collector::add_consumer() {
	uint32_t new_id = ++idpool;
	queues.insert(make_pair(new_id, 
	                        deque<shared_ptr<wrapped_data> >()));
	return new_id;
}

void collector::retire_consumer(uint32_t x) {
	queues.erase(x);
}
