/* standard C libraries */
#include <cstdlib>
#include <cstring>
#include <cassert>

/* standard C++ libraries */
#include <iostream>
#include <utility>
#include <iomanip>
#include <fstream>
#include <unordered_map>

/* standard unix libraries */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

/* external libraries */
#include <boost/program_options.hpp>


#include "netwrap.hpp"
#include "wrapped_buffer.hpp"
#include "logger.hpp"
#include "config.hpp"
#include "connector.hpp"
#include "lib.hpp"

using namespace std;

#define TIMEOUT (2*60*60)

//#define FIND_CXN

struct bitcoin_log_format {
	uint32_t source_id;
	uint8_t type;
	uint64_t timestamp;
	uint32_t id ;
	uint32_t update_type; //see above
	sockaddr_in remote_addr;
	sockaddr_in local_addr;
	uint32_t text_len;
	char text[0];
} __attribute__((packed));

struct bitcoin_msg_log_format {
	uint32_t source_id;
	uint8_t type;
	uint64_t timestamp;
	uint32_t id ;
	uint8_t is_sender;
	struct bitcoin::packed_message msg;
} __attribute__((packed));

bool is_private(uint32_t ip) {
	/* endian assumptions live here */
	return
		(0x000000FF & ip) == 10  || (0x000000FF & ip) == 127  || 
		(0x0000FFFF & ip) == (192 | (168 << 8)) ||
		(0x0000F0FF & ip) == (172 | (16 << 8)); 
}


uint64_t g_time;
set<sockaddr_in, sockaddr_cmp> g_to_connect;
unordered_map<sockaddr_in, uint64_t, sockaddr_hash, sockaddr_keyeq> g_last_fail;

inline void do_insert(const struct sockaddr_in &x) {
	if (!is_private(x.sin_addr.s_addr)) {
		auto it = g_last_fail.find(x);
		if (it != g_last_fail.cend()) {
			uint64_t last_fail = it->second;
			if (last_fail < (g_time - TIMEOUT)) {
				g_to_connect.insert(x);
			} 
		} else {
			g_to_connect.insert(x);
		}
	}
}

void handle_message(const struct log_format *log) {
	if (log->type == BITCOIN) {
		const struct bitcoin_log_format *blf = (const struct bitcoin_log_format *)log;
		uint32_t update_type = ntoh(blf->update_type);
		if ((update_type) & (CONNECT_SUCCESS | ACCEPT_SUCCESS)) {
			do_insert(blf->remote_addr);
		} else if (update_type & CONNECT_FAILURE) {
			uint64_t fail_ts = ntoh(blf->timestamp);
			g_last_fail[blf->remote_addr] = fail_ts;
			if (fail_ts > (g_time - TIMEOUT)) {
				g_to_connect.erase(blf->remote_addr);
			}
		}
	} else if (log->type == BITCOIN_MSG) {
		/* and is payload */
		const struct bitcoin_msg_log_format *blog = (const struct bitcoin_msg_log_format*)log;
		if (! blog->is_sender && strcmp(blog->msg.command, "addr") == 0) {
			uint8_t bits = 0;
			uint64_t entries = bitcoin::get_varint(blog->msg.payload, &bits);
			const struct bitcoin::full_packed_net_addr *addrs = (const struct bitcoin::full_packed_net_addr*) ((uint8_t*)blog->msg.payload + bits);
			struct sockaddr_in to_insert;
			bzero(&to_insert, sizeof(to_insert));
			for(size_t i = 0; i < entries; ++i) {
				if (!is_private(addrs[i].rest.addr.ipv4.as.number)) {
					memcpy(&to_insert.sin_addr, &addrs[i].rest.addr.ipv4.as.in_addr, sizeof(to_insert.sin_addr));
					to_insert.sin_port = addrs[i].rest.port;
					to_insert.sin_family = AF_INET;
					do_insert(to_insert);
				}
			}
		}
	}
}



volatile int lastpid = 0;
void sigchld(int) {
	int status = 0;
	pid_t ret;
	do {
		ret = waitpid(-1, &status, WNOHANG);
		if (ret > 0) {
			lastpid = ret;
		}
	} while (ret > 0);
}

int main(int argc, char *argv[]) {

	if (startup_setup(argc, argv) != 0) {
		return EXIT_FAILURE;
	}

	struct sigaction sigact;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_handler = sigchld;
	sigact.sa_flags = 0;

	const libconfig::Config *cfg(get_config());

	string g_logpath = (const char*)cfg->lookup("verbatim.logpath") ;
	string filename = g_logpath + "/" + "verbatim.seed";

	for(;;) {

		g_time = time(NULL);
		g_to_connect.clear();

		int in_fd = open(filename.c_str(), O_RDONLY);
		if (in_fd < 0) {
			cerr << "Could not open input file: " << strerror(errno) << endl;
			return EXIT_FAILURE;
		}

		cerr << "reading in log" << endl;
		bool reading_len(true);
		wrapped_buffer<uint8_t> buffer(sizeof(uint32_t));
		size_t remaining = sizeof(uint32_t);
		size_t cursor = 0;
		while(true) {
			buffer.realloc(cursor + remaining);
			ssize_t got = read(in_fd, buffer.ptr(), remaining);
			if (got > 0) {
				remaining -=  got;
			} else if (got == 0) {
				break;
			} else if (got < 0) {
				cerr << "Bad read: " << strerror(errno);
			}

			if (remaining == 0) {
				if (reading_len) {
					cursor = 0;
					remaining = ntoh(*(uint32_t*) (buffer.const_ptr()));
					reading_len = false;
				} else {
					const struct log_format *log = (const struct log_format*) buffer.const_ptr(); 
					/* endianness still in nbo order for log */
					handle_message(log);
					reading_len = true;
					remaining = 4;
				}
			}
		}

		close(in_fd);

		/* okay, now we know everyone we might want to connect to. */


		/* send a log message out */
		string root((const char*)cfg->lookup("logger.root"));
		string logpath = root + "servers";
		try {
			g_log_buffer = new log_buffer(unix_sock_client(logpath, true));
		} catch (const network_error &e) {
			cerr << "WARNING: Could not connect to log server! " << e.what() << endl;
		}

		time_t start_time = time(NULL);

		/* remove existing nodes to connect to */
#ifndef FIND_CXN
		int sock = unix_sock_client((const char*)cfg->lookup("connector.control_path"), false);
	
		g_log<DEBUG>("Fetching existing connections...");
		cerr << "Fetching existing connections..." << endl;
		get_all_cxn(sock, [&](struct ctrl::connection_info *info, size_t) {
				g_to_connect.erase(info->remote_addr);
			});
#else
		int sock(0);
#endif

		cerr << "want to connect to " << g_to_connect.size() << " people" << endl;

#ifdef FIND_CXN
		exit(0);
#endif

		/* initiate connections */
		struct sockaddr_in local_addr;
		bzero(&local_addr, sizeof(local_addr));
		local_addr.sin_family = AF_INET;
		if (inet_pton(AF_INET, "127.0.0.1", &local_addr.sin_addr) != 1) {
			perror("inet_pton source");
			return EXIT_FAILURE;
		}
		local_addr.sin_port = hton(static_cast<uint16_t>(0xdead));
		g_log<DEBUG>("Initiating new connections...");
		cerr << "Initiating new connections..." << endl;
		int count = 0;
		for(auto it = g_to_connect.cbegin(); it != g_to_connect.cend(); ++it) {

			ctrl::easy::connect_msg message(&*it, &local_addr);
			pair<wrapped_buffer<uint8_t>, size_t> p = message.serialize();
			do_write(sock, p.first.const_ptr(), p.second);

			if ((count++ % 7001) == 0) { 
				sleep(1);
			}
		}

		cerr << "Waiting...\n";
		time_t now = time(NULL);
		do {
		  sleep(60*60 - (now - start_time));
		  now = time(NULL);
		} while(60*60 > (now - start_time));
		

		filename = g_logpath + "/" + "verbatim.log";

	}

	return EXIT_SUCCESS;
}
