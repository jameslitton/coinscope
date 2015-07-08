#ifndef NETWRAP_HPP
#define NETWRAP_HPP

#include <cstring>
#include <cassert>

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


/* blocking writer, but EINTR can happen */
inline void send_n(int fd, const void *ptr, size_t len) {
	size_t so_far = 0;
	do {
		ssize_t r = send(fd, (const char *)ptr + so_far, len - so_far, MSG_NOSIGNAL);
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


/* blocking writer, but EINTR can happen */
inline void recv_n(int fd, void *ptr, size_t len) {
	size_t so_far = 0;
	do {
		ssize_t r = recv(fd, (char *)ptr + so_far, len - so_far, MSG_WAITALL);
		if (r > 0) {
			so_far += r;
		} else if (r < 0) {
			assert(errno != EAGAIN && errno != EWOULDBLOCK); /* don't use this with non-blockers */
			if (errno != EINTR) {
				throw std::runtime_error(strerror(errno));
			}
		} else { /* r == 0, disconnected */
			throw std::runtime_error("Socket disconnected");
		}
	} while ( so_far < len);
}




#endif
