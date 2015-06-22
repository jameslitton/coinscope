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
#include <libgen.h>

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

using namespace std;

namespace bc = bitcoin;

class child_data {
private:
	pid_t pid;
	int sock;
public:
	child_data() : pid(-1), sock(-1) {}
	child_data(pid_t p, int s) : pid(p), sock(s) {}
	child_data(child_data &&moved) : pid(moved.pid), sock(moved.sock) {
		moved.sock = -1;
	}
	child_data & operator=(child_data &&moved) {
		if (sock != -1) {
			close(sock);
		}
		sock = moved.sock;
		moved.sock = -1;
		pid = moved.pid;
		return *this;
	}
	~child_data() { if (sock != -1) close(sock); };

private:
	child_data & operator=(const child_data &other);
	child_data(const child_data &o);
};

map<int, child_data> childmap; /* idx -> child_data */

void standup_getup_children(char *argv[], const int *which) { /* which is terminated by -1, a list of which children to spin up */
	vector<pair< pid_t, int> > children; /* vector location specifies index */
	const libconfig::Config *cfg(get_config());

	for(;*which >= 0; ++which) {
		cerr << "Doing " << *which << endl;
		int sv[2];
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
			throw network_error(strerror(errno), errno);
		}
		pid_t pid = fork();
		char buffer[sizeof(*which)];
		if (pid < 0) {
			throw network_error(strerror(errno), errno); 
		} else if(pid) { /* in parent */
			// talk to the child over sv[0]
			close(sv[1]);
			int nidx = hton(*which);
			memcpy(buffer, &nidx, sizeof(nidx));
			size_t needed = sizeof(nidx);
			childmap[ *which ] = child_data(pid, sv[0]);
			while(needed > 0) {
			ssize_t got = send(sv[0], buffer + sizeof(nidx) - needed, needed, MSG_NOSIGNAL);
				if (got > 0) {
					needed -= got; 
				} else if (got < 0) {
					cerr << "Failed to send idx to child, so death to him " << pid << ": " << strerror(errno) << endl;
					kill(pid, SIGKILL);
					needed = 0;
					/* sv[0] is invalid in some way presumably, but this should be handled by sigchld when it cleans this out of childmap */
				}
			}

		} else { /* in child */
			// We will assume file descriptor 3 is the channel for the
			// child, so map sv[1] to be the child descriptor
			if (dup2(sv[1], 3) != 3) {
				throw runtime_error(strerror(errno));
			}
			/* this is cludgy, but who cares */
			string path(dirname(argv[0]));
			path += "/main";
			string config_arg("--configfile="); config_arg += string("=") + ((const char*)cfg->lookup("version").getSourceFile());
			string daemon_arg("--daemonize=0");
			string tom_arg("--tom=1");
			execl(path.c_str(), config_arg.c_str(), daemon_arg.c_str(), tom_arg.c_str(), (char*)NULL);
			throw runtime_error(strerror(errno));
		}
	}
}

/* because we do fork/exec in here, the assumption is that everything
   is CLOEXEC or is appropriately handled in standup/getup children */
int main(int argc, char *argv[]) {


	if (startup_setup(argc, argv) != 0) {
		return EXIT_FAILURE;
	}

	const libconfig::Config *cfg(get_config());
	const char *config_file = cfg->lookup("version").getSourceFile();

	signal(SIGPIPE, SIG_IGN);

	cerr << "Connecting to log server" << endl;

	/* okay, stand up children now. Note: nothing but stdin, stdout,
	   and sterr are open right now. If this changes, cleanup in this
	   function. */
	int which[] = {0,1,-1};
	standup_getup_children(argv,which);

	return 0;

	string root((const char*)cfg->lookup("logger.root"));
	string logpath = root + "servers";
	try {
		g_log_buffer = new log_buffer(unix_sock_client(logpath, true));
	} catch (const network_error &e) {
		cerr << "WARNING: Could not connect to log server! Will reattempt" << e.what() << endl;
	}


	

	ev::timer logwatch;
	logwatch.set<log_watcher>(&logpath);
	logwatch.set(10.0, 10.0);
	logwatch.start();

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
		g_log<CONNECTOR>("Initiating GC with commit: ", commit_hash);
		g_log<CONNECTOR>("Full config: ", s);
		cfile.close();
	}
	
	while(true) {
		/* add timer to attempt recreation of lost control channel */
		loop.run();
	}
	
	g_log<CONNECTOR>("Orderly shutdown");
	return EXIT_SUCCESS;
}
