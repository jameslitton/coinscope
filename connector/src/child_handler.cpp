#include <iostream>
#include <iterator>
#include <sstream>
#include <algorithm>


#include "child_handler.hpp"
#include "netwrap.hpp"
#include "network.hpp"


using namespace std;

child_handler::child_handler() : pid(-1), sock(-1), messages() {}

child_handler::child_handler(pid_t p, int s) : pid(p), sock(s), messages() {}

child_handler::child_handler(child_handler &&moved) : pid(moved.pid), sock(moved.sock), messages(moved.messages) {
	moved.sock = -1;
}
child_handler & child_handler::operator=(child_handler &&moved) {
	if (sock != -1) {
		close(sock);
	}
	sock = moved.sock;
	moved.sock = -1;
	pid = moved.pid;
	messages = moved.messages;
	return *this;
}

void child_handler::send(const void *buf, size_t len) {
	/* TODO: handle error condition for child */
	send_n(sock, buf, len);
}

void child_handler::send(const wrapped_buffer<uint8_t> buf, size_t len) {
	send(buf.const_ptr(), len);
}

wrapped_buffer<uint8_t> child_handler::recv(size_t len) {
	/* TODO: handle error condition for child */
	wrapped_buffer<uint8_t> buffer(len);
	recv_n(sock, buffer.ptr(), len);
	return buffer;
}

child_handler::~child_handler() { if (sock != -1) close(sock); };
