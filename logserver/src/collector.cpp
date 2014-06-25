#include <iostream>
#include <utility>

#include "collector.hpp"

using namespace std;

void collector::append(cvector<uint8_t> data) {
	
	shared_ptr<cvector<uint8_t> > w(new cvector<uint8_t>(move(data)));
	for(auto it = queues.begin(); it != queues.end(); ++it) {
		it->second.push_front(w);
		int events = it->first->get_events();
		it->first->set_events( events | ev::WRITE);
	}
}

shared_ptr<cvector<uint8_t>> collector::pop(output_cxn::handler *h) {
	shared_ptr<cvector<uint8_t> > rv;
	auto it = queues.find(h);
	if (it == queues.end()) {
		cerr << "LOGIC ERROR, tried to pop non-existant id " << hex << h;
		/* TODO: HANDLE LOGIC ERROR */
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
	                        deque<shared_ptr<cvector<uint8_t > > >()));
}

void collector::retire_consumer(output_cxn::handler *h) {
	queues.erase(h);
}
