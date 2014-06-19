/* standard C libraries */
#include <cstdlib>
#include <cstring>
#include <cassert>


/* standard C++ libraries */
#include <vector>
#include <set>
#include <random>
#include <iostream>
#include <utility>

/* standard unix libraries */
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

/* third party libraries */
#include <ev++.h>

/* our libraries */
#include "bitcoin.hpp"
#include "bitcoin_handler.hpp"
#include "command_handler.hpp"
#include "iobuf.hpp"
#include "netwrap.hpp"

using namespace std;

namespace bc = bitcoin;
   

void do_parent(vector<int> &fds) {
	ev::default_loop loop;
	//myclass obj;
	vector<ev::io *> watchers;

	ev::timer timer;
	//timer.set<myclass, &myclass::timer_cb>(&obj);
	timer.set(5, 1);
	timer.start();

	for(auto it = fds.cbegin(); it != fds.cend(); ++it) {
		ev::io *iow = new ev::io();
		//iow->set<myclass, &myclass::io_cb>(&obj);
		iow->set(*it, ev::READ);
		iow->start();
		watchers.push_back(iow);
	}

	fds.clear();

	while(true) {
		loop.run();
		cerr << "here\n";
	}

}

class bitcoin_collection { /* place holder */
public:
	pair<size_t, shared_ptr< bc::handler > > alloc_handler() {
		shared_ptr<bc::handler> h(new bc::handler());
		collection.push_back(h);
		return make_pair(collection.size() - 1, h);
	}
	vector<shared_ptr< bc::handler> > collection;
};


bitcoin_collection g_bitcoin_handlers;
set<ev::io *> watchers;

namespace bitcoin {
class accept_handler {
public:
	void io_cb(ev::io &watcher, int revents) {
		struct sockaddr;
		size_t len;
		int client;
		try {
			client = Accept(watcher->fd, &sockaddr, &len);
		} catch (network_error &e) {
			if (e.error_num() != EWOULDBLOCK && e.error_num != EAGAIN) {
				cerr << e.what() << endl;
				watcher->stop;
				close(watcher->fd);
				if (watchers.erase(watcher)) {
					delete this; /* weird */
				}
			}
			return;
		}

		auto p = g_bitcoin_handlers.alloc_handler();
		
		ev::io *iow(new ev::io());
		iow->set<bitcoin::handler, bitcoin::handler::io_cb>(p->second.get());
		iow->start();
		watchers.push_back(iow);
	}
};
};

namespace ctrl{
class accept_handler {
public:
	void io_cb(ev::io &watcher, int revents) {
		struct sockaddr;
		size_t len;
		int client;
		try {
			client = Accept(watcher->fd, &sockaddr, &len);
		} catch (network_error &e) {
			if (e.error_num() != EWOULDBLOCK && e.error_num != EAGAIN) {
				cerr << e.what() << endl;
				watcher->stop;
				close(watcher->fd);
				if (watchers.erase(watcher)) {
					delete this;
				}
			}
			return;
		}

		ev::io *iow(new ev::io());
		iow->set<ctrl::handler, ctrl::handler::io_cb>(p->second.get());
		iow->start();
		watchers.push_back(iow);
	}
};
};

set<bitcoin::accept_handler *> bc_accept_handlers;
set<ctrl::accept_handler *> ctrl_accept_handlers;




int main(int argc, const char *argv[]) {

	char control_filename[] = "/tmp/bitcoin_control";
	unlink(control_filename);

	struct sockaddr_un control_addr;
	bzero(&control_addr, sizeof(control_addr));
	strcpy(control_addr.sun_path, control_filename);

	int control_sock = Socket(AF_UNIX, SOCK_STREAM, 0);
	fcntl(control_sock, F_SETFD, O_NONBLOCK);
	Bind(control_sock, &addr, strlen(control_addr.sun_path) + 
	     sizeof(control_addr.sun_family));
	Listen(control_sock, 5);

	ev::default_loop loop;
	ctrl::accept_handler ctrl_handler;
	ev::io ctrl_accept;

	ctrl_accept->set<ctrl::accept_handler, & ctrl::accept_handler::io_cb>(&ctrl_handler);


	
   
	return EXIT_SUCCESS;
}
