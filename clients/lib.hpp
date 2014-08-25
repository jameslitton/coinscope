#ifndef LIB_HPP
#define LIB_HPP

#include <cassert>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include <stdexcept>
#include <functional>

#include "network.hpp"
#include "command_structures.hpp"

/* blocking writer, but EINTR can happen */
inline void do_write(int fd, const void *ptr, size_t len) {
	size_t so_far = 0;
	do {
		ssize_t r = write(fd, (const char *)ptr + so_far, len - so_far);
		if (r > 0) {
			so_far += r;
		} else if (r < 0) {
			assert(errno != EAGAIN && errno != EWOULDBLOCK); /* don't use this with non-blockers */
			if (errno != EINTR) {
				throw std::runtime_error(strerror(errno));
			}
		}
	} while ( so_far < len);
}



/* assumes sock is blocking connection to control channel, returns
   number of connections received */

uint32_t get_all_cxn_prefix(int sock) {
	uint32_t alloc_size = sizeof(struct ctrl::message) + sizeof(struct ctrl::command_msg);
	struct ctrl::message *msg = (struct ctrl::message*) ::operator new(alloc_size);
	bzero(msg, sizeof(*msg));

	msg->version = 0;
	msg->length = hton((uint32_t)sizeof(struct ctrl::command_msg));
	msg->message_type = ctrl::COMMAND;
	struct ctrl::command_msg *cmsg = (struct ctrl::command_msg*) &msg->payload;
	bzero(cmsg, sizeof(*cmsg));
	cmsg->command = ctrl::COMMAND_GET_CXN;

	do_write(sock, msg, alloc_size);

	delete msg;

	uint32_t len;
	if (recv(sock, &len, sizeof(len), MSG_WAITALL) != sizeof(len)) {
		throw std::runtime_error(strerror(errno));
	}
	len = ntoh(len);
	return len;
}

template <typename C>
size_t get_all_cxn(int sock, C callback) {

	uint32_t len = get_all_cxn_prefix(sock);
	assert((len*1.0) / sizeof(struct ctrl::connection_info) - (len / sizeof(struct ctrl::connection_info)) == 0);
	size_t addresses = len / sizeof(struct ctrl::connection_info);
	if (addresses == 0) {
		return 0;
	}

	size_t i = 0;
	while(len > 0) {
		struct ctrl::connection_info info;
		if (recv(sock, &info, sizeof(info), MSG_WAITALL) != sizeof(info)) {
			throw std::runtime_error(strerror(errno));
		} else {
			callback(&info, i++);
			len -= sizeof(info);
		}
	}
	return addresses;
}



template <typename Cgen, typename C>
size_t get_all_cxn(int sock, Cgen callgen) {
	uint32_t len = get_all_cxn_prefix(sock);
	assert((len*1.0) / sizeof(struct ctrl::connection_info) - (len / sizeof(struct ctrl::connection_info)) == 0);
	size_t addresses = len / sizeof(struct ctrl::connection_info);
	if (addresses == 0) {
		return 0;
	}
	C callback(callgen(addresses));

	size_t i = 0;
	while(len > 0) {
		struct ctrl::connection_info info;
		if (recv(sock, &info, sizeof(info), MSG_WAITALL) != sizeof(info)) {
			throw std::runtime_error(strerror(errno));
		} else {
			callback(&info, i++);
			len -= sizeof(info);
		}
	}
	return addresses;
}


struct connect_message {
	uint8_t version;
	uint32_t length;
	uint8_t message_type;
	struct ctrl::connect_payload payload;
} __attribute__((packed));


#endif
