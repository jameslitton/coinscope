#ifndef ACCEPT_HANDLER_HPP
#define ACCEPT_HANDLER_HPP

#include <fcntl.h>

#include "netwrap.hpp"


#include <ev++.h>

namespace handlers {

template <typename T>
class accept_handler { 
public:
	accept_handler(int fd) : io() {
		io.set<accept_handler<T>, &accept_handler<T>::io_cb>(this);
		io.set(fd, ev::READ);
		io.start();
	}
	~accept_handler() {
		io.stop();
		close(io.fd);
	}
	void io_cb(ev::io &watcher, int /* revents */) {
		int client;
		try {
			client = Accept(watcher.fd, NULL, NULL);
			fcntl(client, F_SETFL, O_NONBLOCK);		
		} catch (network_error &e) {
			if (e.error_code() != EWOULDBLOCK && e.error_code() != EAGAIN && e.error_code() != EINTR) {
				T::handle_accept_error(this, e);
			}
			return;
		}
		T::handle_accept(this, client);
	}
private:
	ev::io io;
	friend void T::handle_accept(accept_handler<T> *h, int fd);
	friend void T::handle_accept_error(accept_handler<T> *h, const network_error &e);
};

};

#endif
