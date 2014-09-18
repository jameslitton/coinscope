/* standard C libraries */
#include <cstdlib>
#include <cstring>
#include <cassert>

/* standard C++ libraries */
#include <iostream>
#include <utility>
#include <iomanip>
#include <fstream>

/* standard unix libraries */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>


#include "netwrap.hpp"
#include "read_buffer.hpp"
#include "logger.hpp"
#include "config.hpp"


using namespace std;


bool g_prerotate = false;
bool g_postrotate = false;

fstream logout;

void sigusr(int num) {
	if (num == SIGUSR1) {
		g_prerotate = true;
	} else if (num == SIGUSR2) {
		g_postrotate = true;
	}
}


void print_message(read_buffer &input_buf) {
	const uint8_t *buf = input_buf.extract_buffer().const_ptr();
	logout.write((const char*)buf, input_buf.cursor());
}

void open_log() {
	const libconfig::Config *cfg(get_config());
	if (logout.is_open()) {
		logout.flush();
		logout.close();
	}
	string g_logpath = (const char*)cfg->lookup("verbatim.logpath");
	/* never actually use as input, but reasons */
	logout.open(g_logpath + "verbatim.log", ios::out | ios::in | ios::ate | ios::binary );
	if (!logout.is_open()) {
		cerr << "Could not open log\n";
		abort();
	}
}


/* just redirect to stdout. redirect this to the file you actually want it to go to */

int main(int argc, char *argv[]) {
	if (argc == 2) {
		load_config(argv[1]);
	} else {
		load_config("../netmine.cfg");
	}

	const libconfig::Config *cfg(get_config());

	string root((const char*)cfg->lookup("logger.root"));

	mkdir(root.c_str(), 0777);
	string client_dir(root + "clients/");


	int client = unix_sock_client(client_dir + "all", false);

	open_log();

	struct sigaction sigact;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_handler = sigusr;
	sigact.sa_flags = 0;
	sigaction(SIGUSR1, &sigact, NULL);
	sigaction(SIGUSR2, &sigact, NULL);

	bool reading_len(true);

	read_buffer input_buf(sizeof(uint32_t));

	while(true) {
		auto ret = input_buf.do_read(client);
		int r = ret.first;
		if (r == 0) {
			cerr << "Disconnected\n";
			return EXIT_SUCCESS;
		} else if (r < 0 && errno == EINTR) {
			/* fine */
		} else if (r < 0) {
			cerr << "Got error, " << strerror(errno) << endl;
			return EXIT_FAILURE;
		}

		if (g_prerotate) { /* do log rotation if necessary. Don't write until rotation finished (i.e., received postrotate) */
			logout.flush();
			logout.close();

			while(!g_postrotate) {
				sleep(1);
			}

			open_log();
			g_prerotate = g_postrotate = false;
		}

		if (!input_buf.hungry()) {
			if (reading_len) {
				uint32_t netlen = *((const uint32_t*) input_buf.extract_buffer().const_ptr());
				input_buf.to_read(ntoh(netlen));
				reading_len = false;
			} else {
				print_message(input_buf);
				input_buf.cursor(0);
				input_buf.to_read(4);
				reading_len = true;
			}
		}
	}

}
