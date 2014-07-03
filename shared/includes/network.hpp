#ifndef NETWORK_HPP
#define NETWORK_HPP

#include <endian.h>

inline uint32_t hton(uint32_t x) {
	return htobe32(x);
}

inline uint16_t hton(uint16_t x) {
	return htobe16(x);
}

inline uint32_t ntoh(uint32_t x) {
	return be32toh(x);
}

inline uint16_t ntoh(uint16_t x) {
	return be16toh(x);
}

inline uint64_t ntoh(uint64_t x) {
	return be64toh(x);
}

inline uint64_t hton(uint64_t x) {
	return htobe64(x);
}

struct sockaddr_un;

int unix_sock_setup(const char *path, struct sockaddr_un *addr, bool nonblocking);
int unix_sock_server(const char *path, int listen, bool nonblocking);
int unix_sock_client(const char *path, bool nonblocking);


#endif
