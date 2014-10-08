/* standard C libraries */
#include <cstdlib>
#include <cstring>
#include <cassert>

/* standard C++ libraries */
#include <iostream>
#include <utility>

/* standard unix libraries */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>


#include "lib.hpp"
#include "network.hpp"
#include "connector.hpp"
#include "bitcoin.hpp"
#include "config.hpp"
#include "logger.hpp"
#include "bcwatch.hpp"

/* This is a simple test client to demonstrate use of the connector */


using namespace std;
using namespace ctrl::easy;

struct in_addr g_local_addr;


int main(int argc, char *argv[]) {

	if (startup_setup(argc, argv) != 0) {
		return EXIT_FAILURE;
	}

	const libconfig::Config *cfg(get_config());

	libconfig::Setting &list = cfg->lookup("connector.bitcoin.listeners");
	if (list.getLength() == 0) {
		return EXIT_FAILURE;
	}

	libconfig::Setting &local_cxn = list[0];
	if (inet_aton("172.31.30.19", &g_local_addr) == 0) {
		cerr << "bad aton on " << (const char *)local_cxn[1] << endl;
		return EXIT_FAILURE;
	}




	int sock = unix_sock_client((const char*)cfg->lookup("connector.control_path"), false);

	string root((const char*)cfg->lookup("logger.root"));
	mkdir(root.c_str(), 0777);
	string client_dir(root + "clients/");
	int bitcoin_client = unix_sock_client(client_dir + "bitcoin", false);
	bcwatch watcher(bitcoin_client, 
	                [](unique_ptr<struct bc_channel_msg>) {
		                /* successful connect, we don't care */
	                },
	                [&](unique_ptr<struct bc_channel_msg> msg) {
		                if (msg->update_type & (ORDERLY_DISCONNECT| WRITE_DISCONNECT | PEER_RESET)) {

			                struct sockaddr_in remote_addr;
			                if (msg->remote.sin_addr.s_addr == g_local_addr.s_addr) {
				                /* they connected to us, so local is our remote */
				                memcpy(&remote_addr, &msg->local, sizeof(remote_addr));
			                } else if (msg->local.sin_addr.s_addr == g_local_addr.s_addr) {
				                memcpy(&remote_addr, &msg->remote, sizeof(remote_addr));
			                } else {
				                cout << msg->remote << " and " << msg->local << " matched no one\n";
				                return;
			                }

			                cout << "Reconnecting " << msg->remote << endl;


			                /* doesn't matter, connector doesn't pay attention to this right now */
			                struct sockaddr_in local_addr;
			                bzero(&local_addr, sizeof(local_addr));
			                local_addr.sin_family = AF_INET;
			                if (inet_pton(AF_INET, "127.0.0.1", &local_addr.sin_addr) != 1) {
				                perror("inet_pton source");
				                return;
			                }
			                local_addr.sin_port = hton(static_cast<uint16_t>(0xdead));

			                connect_msg message(&remote_addr, &local_addr);

			                pair<wrapped_buffer<uint8_t>, size_t> p = message.serialize();

			                do_write(sock, p.first.ptr(), p.second);
			                
		                }
	                });

	watcher.loop_forever();
	
	return EXIT_SUCCESS;
};
