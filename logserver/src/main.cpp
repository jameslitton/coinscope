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

/* third party libraries */
#include <ev++.h>

#include "network.hpp"
#include "accept_handler.hpp"
#include "input_cxn.hpp"
#include "output_cxn.hpp"
#include "logger.hpp"
#include "config.hpp"

using namespace std;

int main(int argc, char * argv[] ) {

	if (startup_setup(argc, argv) != 0) {
		return EXIT_FAILURE;
	}

	const libconfig::Config *cfg(get_config());
	signal(SIGPIPE, SIG_IGN);

	string root((const char*)cfg->lookup("logger.root"));

	/* TODO: make configurable */
	mkdir(root.c_str(), 0777);
	string client_dir(root + "clients/");
	mkdir(client_dir.c_str(), 0777);


	/* TODO: clean this up, just leaving as POC for now */
	handlers::accept_handler<output_cxn::handler> debug_handler(unix_sock_server(client_dir + "debug", 5, true));
	output_cxn::handler::set_interest(&debug_handler, (uint8_t)DEBUG);

	handlers::accept_handler<output_cxn::handler> ctrl_handler(unix_sock_server(client_dir + "ctrl", 5, true));
	output_cxn::handler::set_interest(&ctrl_handler, (uint8_t)CTRL);

	handlers::accept_handler<output_cxn::handler> error_handler(unix_sock_server(client_dir + "error", 5, true));
	output_cxn::handler::set_interest(&error_handler, (uint8_t)ERROR);

	handlers::accept_handler<output_cxn::handler> bitcoin_handler(unix_sock_server(client_dir + "bitcoin", 5, true));
	output_cxn::handler::set_interest(&bitcoin_handler, (uint8_t)BITCOIN);

	handlers::accept_handler<output_cxn::handler> bitcoin_msg_handler(unix_sock_server(client_dir + "bitcoin_msg", 5, true));
	output_cxn::handler::set_interest(&bitcoin_msg_handler, (uint8_t)BITCOIN_MSG);

	handlers::accept_handler<output_cxn::handler> bitcoin_foo_handler(unix_sock_server(client_dir + "bitcoinx", 5, true));
	output_cxn::handler::set_interest(&bitcoin_foo_handler, (uint8_t)(BITCOIN_MSG | BITCOIN));

	handlers::accept_handler<output_cxn::handler> all_handler(unix_sock_server(client_dir + "all", 5, true));
	output_cxn::handler::set_interest(&all_handler, (uint8_t)~0);


	ev::default_loop loop;


	handlers::accept_handler<input_cxn::handler> in_handler(unix_sock_server(root + "servers", 5, true));

	while(true) {
		loop.run();
	}

	/* TODO: close and cleanup all output accept handlers. Doesn't matter because we are exiting now */

	return EXIT_SUCCESS;
}

