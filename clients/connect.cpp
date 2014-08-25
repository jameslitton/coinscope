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
#include "command_structures.hpp"
#include "bitcoin.hpp"
#include "config.hpp"
#include "logger.hpp"
#include "bcwatch.hpp"

/* This is a simple test client to demonstrate use of the connector */


using namespace std;
using namespace ctrl;

struct connect_message {
	uint8_t version;
	uint32_t length;
	uint8_t message_type;
	struct connect_payload payload;
} __attribute__((packed));

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
	bcwatch watcher(bitcoin_client, 
	                [](unique_ptr<struct bc_channel_msg> msg) {
		                cout << "Successful connect. Details follow: " << endl;
		                cout << "\ttime: " << msg->time << endl;
		                cout << "\thandle_id: " << msg->handle_id << endl;
		                cout << "\tupdate_type: " << msg->update_type << endl;
		                cout << "\tremote: " << *((struct sockaddr*) &msg->remote) << endl;
		                cout << "\tlocal: " << *((struct sockaddr*) &msg->local) << endl;
		                cout << "\ttext_length: " << msg->text_length << endl;
		                if (msg->text_length) {
			                cout << "\ttext: " << msg->text << endl;
		                }
	                },
	                [](unique_ptr<struct bc_channel_msg> msg) {
		                cout << "Unsuccessful connect. Details follow: " << endl;
		                cout << "\ttime: " << msg->time << endl;
		                cout << "\tupdate_type: " << msg->update_type << endl;
		                cout << "\tremote: " << *((struct sockaddr*) &msg->remote) << endl;
		                cout << "\ttext_length: " << msg->text_length << endl;
		                if (msg->text_length) {
			                cout << "\ttext: " << msg->text << endl;
		                }
	                });
	                

#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
	struct connect_message message = { 0, hton((uint32_t)sizeof(connect_payload)), CONNECT, {{0},{0}} };
#pragma GCC diagnostic warning "-Wmissing-field-initializers"

	

	message.payload.remote_addr.sin_family = AF_INET;
	if (inet_pton(AF_INET, "127.0.0.1", &message.payload.remote_addr.sin_addr) != 1) {
		perror("inet_pton destination");
		return EXIT_FAILURE;
	}

	message.payload.local_addr.sin_family = AF_INET;
	if (inet_pton(AF_INET, "127.0.0.1", &message.payload.local_addr.sin_addr) != 1) {
		perror("inet_pton source");
		return EXIT_FAILURE;
	}

	message.payload.remote_addr.sin_port = hton(static_cast<uint16_t>(8333));
	message.payload.local_addr.sin_port = hton(static_cast<uint16_t>(0xdead));

	cout << hton((uint32_t)sizeof(connect_payload)) << endl;
	cout << ntoh(message.length) << endl;

	if (write(sock, &message, sizeof(message)) != sizeof(message)) {
		perror("write");
		return EXIT_FAILURE;
	}

	watcher.loop_once();
	
	return EXIT_SUCCESS;
};
