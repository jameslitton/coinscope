#include <cstring>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>

#include "network.hpp"
#include "netwrap.hpp"

int unix_sock_setup(const std::string &path, struct sockaddr_un *addr, bool nonblocking) {
	bzero(addr, sizeof(*addr));
	addr->sun_family = AF_UNIX;
	strcpy(addr->sun_path, path.c_str());
	int sock = Socket(AF_UNIX, SOCK_STREAM, 0);
	if (nonblocking) {
		fcntl(sock, F_SETFL, O_NONBLOCK);
	}
	return sock;
}

int unix_sock_server(const std::string &path, int listen, bool nonblocking) {
	unlink(path.c_str());

	struct sockaddr_un addr;
	int sock = unix_sock_setup(path, &addr, nonblocking);
	Bind(sock, (struct sockaddr*)&addr, strlen(addr.sun_path) + 
	     sizeof(addr.sun_family));
	Listen(sock, listen);
	return sock;
}

int unix_sock_client(const std::string &path, bool nonblocking) {
	sockaddr_un addr;
	int sock = unix_sock_setup(path, &addr, nonblocking);
	Connect(sock, (struct sockaddr *)&addr, strlen(addr.sun_path) + sizeof(addr.sun_family));
	return sock;
}
