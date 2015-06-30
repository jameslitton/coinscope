#ifndef CHILD_HANDLER_HPP
#define CHILD_HANDLER_HPP

#include "wrapped_buffer.hpp"
#include <map>


class child_handler {
private:
	pid_t pid;
	int sock;
public:
	child_handler();
	child_handler(pid_t p, int sck);
	child_handler(child_handler &&moved);
	child_handler & operator=(child_handler &&moved);
	void send(const void *buf, size_t len);
	void send(const wrapped_buffer<uint8_t> buf, size_t len);
	wrapped_buffer<uint8_t> recv(size_t len);
	~child_handler();

	std::map<uint32_t, uint32_t> messages;

private:
	child_handler & operator=(const child_handler &other);
	child_handler(const child_handler &o);
};


#endif
