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





struct sockaddr_cmp {
	bool operator()(const struct sockaddr_in &lhs, const struct sockaddr_in &rhs) {
		int t = lhs.sin_addr.s_addr - rhs.sin_addr.s_addr;
		if (!t) {
			t = lhs.sin_port - rhs.sin_port;
		}
		return t < 0;
	}
};

bool is_private(uint32_t ip) {
	/* endian assumptions live here */
	return
		(0x000000FF & ip) == 10  || (0x000000FF & ip) == 127  || 
		(0x0000FFFF & ip) == (192 | (168 << 8)) ||
		(0x0000F0FF & ip) == (172 | (16 << 8)); 
}

struct sockaddr_hash {
	size_t operator()(const struct sockaddr_in &a) const {
		return std::hash<uint32_t>()(a.sin_addr.s_addr) + std::hash<uint16_t>()(a.sin_port);
	}
};

struct sockaddr_keyeq {
	bool operator()(const struct sockaddr_in &a, const struct sockaddr_in &b) const {
		return a.sin_addr.s_addr == b.sin_addr.s_addr &&
			a.sin_port == b.sin_port;
	}
};

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
	ssize_t rv = recv(sock, &len, sizeof(len), MSG_WAITALL);
	if (rv == 0) {
		return 0;
	}

	if (rv != sizeof(len)) {
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
