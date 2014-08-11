#ifndef NETWRAP_HPP
#define NETWRAP_HPP

#include <cstring>

#include <unistd.h>
#include <sys/socket.h>


#ifdef __cplusplus
#include <stdexcept>

class network_error : public std::runtime_error {
public: 
	int error_code() const { return err; }
	network_error(const std::string &what, int err) : 
		std::runtime_error(what), err(err){ }
private:
	int err;
};

inline void do_error(int is_error, const std::string &str, int err) {
	if (is_error) {
		throw network_error(str + ": " + strerror(err), err);
	}
}

#else

inline void do_error(int is_error, const char *str, int err) {
	if (is_error) {
		perror(str);
		abort();
	}
}

#endif

inline int Socket(int domain, int type, int protocol) {
	int rv = socket(domain, type, protocol);
	do_error(rv == -1, "socket failure", errno);
	return rv;
}

inline int Bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	int rv = bind(sockfd, addr, addrlen);
	do_error(rv == -1, "bind failure", errno);
	return rv;
}

inline int Listen(int sockfd, int backlog) {
	int rv = listen(sockfd, backlog);
	do_error(rv == -1, "listen failure", errno);
	return rv;
}


inline int Accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
	int rv = accept(sockfd, addr, addrlen);
	do_error(rv == -1, "accept failure", errno);
	return rv;
}

inline int Connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	int rv = connect(sockfd, addr, addrlen);
	do_error(rv == -1, "connect failure", errno);
	return rv;
}

inline int Socketpair(int domain, int type, int protocol, int sv[2]) {
	int rv = socketpair(domain, type, protocol, sv);
	do_error(rv == -1, "Socketpair failure", errno);
	return rv;
}

inline pid_t Fork() {
	pid_t rv = fork();
	do_error(rv < 0, "fork failure", errno);
	return rv;
}

#endif
