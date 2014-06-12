#ifndef NETWORK_HPP
#define NETWORK_HPP

#include <netinet/in.h>

inline uint32_t hton(uint32_t x) {
   return htonl(x);
}

inline uint16_t hton(uint16_t x) {
   return htons(x);
}

inline uint32_t ntoh(uint32_t x) {
   return ntohl(x);
}

inline uint16_t ntoh(uint16_t x) {
   return ntohs(x);
}


#endif
