/* standard C libraries */
#include <cstdlib>
#include <cstring>
#include <cassert>


/* standard C++ libraries */
#include <vector>
#include <set>
#include <random>
#include <utility>
#include <iostream>
#include <fstream>

/* standard unix libraries */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/resource.h>

/* third party libraries */
#include <ev++.h>

/* our libraries */
#include "autogen.hpp"
#include "bitcoin.hpp"
#include "bitcoin_handler.hpp"
#include "command_handler.hpp"
#include "iobuf.hpp"
#include "netwrap.hpp"
#include "logger.hpp"
#include "network.hpp"
#include "config.hpp"
#include "blacklist.hpp"

using namespace std;

namespace bc = bitcoin;

ipaddr_set g_blacklist;


static void log_watcher(ev::timer &w, int /*revents*/) {
	if (g_log_buffer == nullptr) {
		try {
			g_log_buffer = new log_buffer(unix_sock_client(*static_cast<string*>(w.data), true));
		} catch(const network_error &e) {
			g_log_buffer = nullptr;
			g_log<ERROR>(e.what());
		}
	}
}

static void load_blacklist() {
	const libconfig::Config *cfg(get_config());
	const char *filename = cfg->lookup("connector.blacklist");
	ifstream file(filename);;

	if (file) {
		g_blacklist.clear();

		string line;
		struct sockaddr_in entry;
		bzero(&entry, sizeof(entry));
		entry.sin_family = AF_INET;

		while(getline(file, line)) {
			if (inet_pton(AF_INET, line.c_str(), &entry.sin_addr) != 1) {
				g_log<ERROR>("Invalid blacklist entry", line);
			} else {
				g_blacklist.insert(entry);
			}
		}
	
		g_log<CONNECTOR>("Loading blacklist, received", g_blacklist.size(), "unique entries");
	} else {
		g_log<ERROR>("Could not open blacklist file");
	}

	file.close();

}

static void hup_watcher(ev::sig & /*s*/, int /* revents */) {
	load_blacklist();
}


int main(int argc, char *argv[]) {

	/* check limits or no point */

	struct rlimit limit;
	if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
		cerr << "Could not get limit\n";
		return EXIT_FAILURE;
	}

	if (limit.rlim_cur < 999900) {
		cerr << "limit too low, aborting (" << limit.rlim_cur << ")\n";
		return EXIT_FAILURE;
	}


	if (startup_setup(argc, argv) != 0) {
		return EXIT_FAILURE;
	}

	const libconfig::Config *cfg(get_config());
	const char *config_file = cfg->lookup("version").getSourceFile();

	signal(SIGPIPE, SIG_IGN);

	cerr << "Starting up and transferring to log server" << endl;

	string root((const char*)cfg->lookup("logger.root"));
	string logpath = root + "servers";
	try {
		g_log_buffer = new log_buffer(unix_sock_client(logpath, true));
	} catch (const network_error &e) {
		cerr << "WARNING: Could not connect to log server! " << e.what() << endl;
	}


	ev::timer logwatch;
	logwatch.set<log_watcher>(&logpath);
	logwatch.set(10.0, 10.0);
	logwatch.start();

	ev::sig sigwatch;
	sigwatch.set<hup_watcher>();
	sigwatch.set(SIGHUP);
	sigwatch.start();

	const char *control_filename = cfg->lookup("connector.control_path");
	unlink(control_filename);

	struct sockaddr_un control_addr;
	bzero(&control_addr, sizeof(control_addr));
	control_addr.sun_family = AF_UNIX;
	strcpy(control_addr.sun_path, control_filename);

	int control_sock = Socket(AF_UNIX, SOCK_STREAM, 0);
	fcntl(control_sock, F_SETFL, O_NONBLOCK);
	Bind(control_sock, (struct sockaddr*)&control_addr, strlen(control_addr.sun_path) + 
	     sizeof(control_addr.sun_family));
	Listen(control_sock, cfg->lookup("connector.control_listen"));

	ev::default_loop loop;


	vector<unique_ptr<bc::accept_handler> > bc_accept_handlers; /* form is to get around some move semantics with ev::io I don't want to muck it up */

	libconfig::Setting &list = cfg->lookup("connector.bitcoin.listeners");
	for(int index = 0; index < list.getLength(); ++index) {
		libconfig::Setting &setting = list[index];
		string family((const char*)setting[0]);
		string ipv4((const char*)setting[1]);
		uint16_t port((int)setting[2]);
		int backlog(setting[3]);

		g_log<DEBUG>("Attempting to instantiate listener on ", family, 
		               ipv4, port, "with backlog", backlog);

		if (family != "AF_INET") {
			g_log<ERROR>("Family", family, "not supported. Skipping");
			continue;
		}

		struct sockaddr_in bitcoin_addr;
		bzero(&bitcoin_addr, sizeof(bitcoin_addr));
		bitcoin_addr.sin_family = AF_INET;
		bitcoin_addr.sin_port = htons(port);
		if (inet_pton(AF_INET, ipv4.c_str(), &bitcoin_addr.sin_addr) != 1) {
			g_log<ERROR>("Bad address format on address", index, strerror(errno));
			continue;
		}

		/* TEMPORARY HACK!!!! This is because on EC2 the local interface is not the same as the public interface */
		bitcoin_addr.sin_addr.s_addr = INADDR_ANY;


		int bitcoin_sock = Socket(AF_INET, SOCK_STREAM, 0);
		fcntl(bitcoin_sock, F_SETFL, O_NONBLOCK);
		int optval = 1;
		setsockopt(bitcoin_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
		Bind(bitcoin_sock, (struct sockaddr*)&bitcoin_addr, sizeof(bitcoin_addr));
		Listen(bitcoin_sock, backlog);

		bc_accept_handlers.emplace_back(new bc::accept_handler(bitcoin_sock, bitcoin_addr));

	}


	ctrl::accept_handler ctrl_handler(control_sock);


	{
		ifstream cfile(config_file);
		string s("");
		string line;
		for(string line; getline(cfile,line);) {
			s += line + "\n";
		}
		g_log<CONNECTOR>("Initiating with commit: ", commit_hash);
		g_log<CONNECTOR>("Full config: ", s);
		cfile.close();
	}

	load_blacklist();
	
	while(true) {
		/* add timer to attempt recreation of lost control channel */
		loop.run();
	}
	
	g_log<CONNECTOR>("Orderly shutdown");
	return EXIT_SUCCESS;
}
