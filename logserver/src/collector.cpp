#include <iostream>
#include <utility>

#include "collector.hpp"

using namespace std;

void collector::append(unique_ptr<uint32_t[]> data, size_t len) {
	shared_ptr<collector::wrapped_data> w(new collector::wrapped_data(move(data), len));
	for(auto it = queues.begin(); it != queues.end(); ++it) {
		/* TODO it->lock.acquire() */
		it->second.queue.push_front(w);
		/* TODO it->lock.release() */
	}
}

shared_ptr<collector::wrapped_data> collector::pop(uint32_t id) {
	shared_ptr<collector::wrapped_data> rv;
	auto it = queues.find(id);
	if (it == queues.end()) {
		cerr << "LOGIC ERROR, tried to pop non-existant id " << id;
		/* TODO: HANDLE LOGIC ERROR */
	} else {
		/* TODO: acquire(it->lock) */
		if (it->second.queue.size()) {
			rv = it->second.queue.back();
			it->second.queue.pop_back();
		}
		/* TODO: release(it->lock); */
	}
	return rv;
}

uint32_t collector::add_consumer() {
	uint32_t new_id = ++idpool;
	queues.insert(make_pair(new_id, wrapped_deque()));
	return new_id;
}

void collector::retire_consumer(uint32_t x) {
	queues.erase(x);
}
