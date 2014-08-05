#ifndef LIB_HPP
#define LIB_HPP

#include <cassert>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include <stdexcept>
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


struct connect_message {
	uint8_t version;
	uint32_t length;
	uint8_t message_type;
	struct ctrl::connect_payload payload;
} __attribute__((packed));



#endif
