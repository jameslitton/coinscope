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


#include "network.hpp"
#include "connector.hpp"
#include "bitcoin.hpp"
#include "config.hpp"
#include "logger.hpp"
#include "bcwatch.hpp"

/* This is a simple test client to demonstrate use of the connector */


using namespace std;
using namespace ctrl::easy;

int main(int argc, char *argv[]) {

	if (argc == 2) {
		load_config(argv[1]);
	} else {
		load_config("../netmine.cfg");
	}

	const libconfig::Config *cfg(get_config());

	int sock = unix_sock_client((const char*)cfg->lookup("connector.control_path"), false);

	string root((const char*)cfg->lookup("logger.root"));
	mkdir(root.c_str(), 0777);
	string client_dir(root + "clients/");
	int bitcoin_client = unix_sock_client(client_dir + "bitcoin", false);
	bcwatchers::bcwatch watcher(bitcoin_client, 
	                [](unique_ptr<bc_channel_msg> msg) {
		                cout << "Successful connect. Details follow: " << endl;
		                cout << "\tsource: " << msg->header.source_id << endl;
		                cout << "\ttime: " << msg->header.timestamp << endl;
		                cout << "\thandle_id: " << msg->handle_id << endl;
		                cout << "\tupdate_type: " << msg->update_type << endl;
		                cout << "\tremote: " << *((struct sockaddr*) &msg->remote_addr) << endl;
		                cout << "\tlocal: " << *((struct sockaddr*) &msg->local_addr) << endl;
		                cout << "\ttext_length: " << msg->text_len << endl;
		                if (msg->text_len) {
			                cout << "\ttext: " << msg->text << endl;
		                }
	                },
	                [](unique_ptr<bc_channel_msg> msg) {
		                cout << "Unsuccessful connect. Details follow: " << endl;
		                cout << "\tsource: " << msg->header.source_id << endl;
		                cout << "\ttime: " << msg->header.timestamp << endl;
		                cout << "\tupdate_type: " << msg->update_type << endl;
		                cout << "\tremote: " << *((struct sockaddr*) &msg->remote_addr) << endl;
		                cout << "\ttext_length: " << msg->text_len << endl;
		                if (msg->text_len) {
			                cout << "\ttext: " << msg->text << endl;
		                }
	                });
	                

	
	struct sockaddr_in remote_addr;
	bzero(&remote_addr, sizeof(remote_addr));
	remote_addr.sin_family = AF_INET;
	if (inet_pton(AF_INET, "xxx173.69.49.106", &remote_addr.sin_addr) != 1) {
		perror("inet_pton destination");
		return EXIT_FAILURE;
	}

	struct sockaddr_in local_addr;
	bzero(&local_addr, sizeof(local_addr));
	local_addr.sin_family = AF_INET;
	if (inet_pton(AF_INET, "127.0.0.1", &local_addr.sin_addr) != 1) {
		perror("inet_pton source");
		return EXIT_FAILURE;
	}

	remote_addr.sin_port = hton(static_cast<uint16_t>(8333));
	local_addr.sin_port = hton(static_cast<uint16_t>(0xdead));

	connect_msg message(&remote_addr, &local_addr);

	pair<wrapped_buffer<uint8_t>, size_t> p = message.serialize();

	if (write(sock, p.first.ptr(), p.second) != static_cast<ssize_t>(p.second)) {
		perror("write");
		return EXIT_FAILURE;
	}

	watcher.loop_once();
	
	return EXIT_SUCCESS;
};
