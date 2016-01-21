#ifndef BLACKLIST_HPP
#define BLACKLIST_HPP

#include <cstdint>

#include <netinet/in.h>
#include <unordered_set>


struct sockaddr_in_hashfunc { /* don't care about ports */
	size_t operator()(const sockaddr_in &h) const {
		/* proposed hash_combine in TR */
		size_t seed = std::hash<decltype(h.sin_family)>{}(h.sin_family) + 0x9e3779b0;
		return seed ^ (std::hash<decltype(h.sin_addr.s_addr)>{}(h.sin_addr.s_addr) + 0x9e3779b0 + (seed << 6) + (seed >> 2));
			
	}
};

struct sockaddr_in_eq {
	bool operator()(const sockaddr_in &lhs, const sockaddr_in &rhs) const {
		return lhs.sin_family == rhs.sin_family && lhs.sin_addr.s_addr == rhs.sin_addr.s_addr;
	}
};


typedef std::unordered_set<struct sockaddr_in, sockaddr_in_hashfunc, sockaddr_in_eq> ipaddr_set;

extern ipaddr_set g_blacklist;


#endif
