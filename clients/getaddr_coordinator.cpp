/* standard C libraries */
#include <cstdlib>
#include <cstring>
#include <cassert>

/* standard C++ libraries */
#include <iostream>
#include <utility>
#include <map>
#include <random>

/* standard unix libraries */
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <signal.h>


#include "config.hpp"


using namespace std;

enum Service { LOGSERVER=0, VERBATIM, GC, CONNECTOR, GETADDR } ;


map<pid_t, Service> g_services;

const char * service_2_binary(Service s) {
	static const char* strings[] = { "/opt/coinscope/logserver/logserver", "/opt/coinscope/logclient/verbatim", "/opt/coinscope/connector/groundctrl", "/opt/coinscope/connector/connector", "/opt/coinscope/clients/getaddr" };
	return strings[s];
}


const char * service_2_str(Service s) {
	static const char* strings[] = { "LOGSERVER", "VERBATIM", "GC", "CONNECTOR", "GETADDR" };
	return strings[s];
}


void sfork(Service s) {
	const libconfig::Config *cfg(get_config());

	pid_t pid = fork();
	if (pid < 0) {
		cerr << "Could not fork: " << strerror(errno) << endl;
		return;
	} else if (pid) { /* in parent */
		g_services[pid] = s;
	} else { /* in child */
		prctl(PR_SET_PDEATHSIG, SIGKILL);
		const char *path(service_2_binary(s));
		string config_arg("--configfile="); config_arg += ((const char*)cfg->lookup("version").getSourceFile());
		string daemon_arg("--daemonize=0");
		execl(path, path, config_arg.c_str(), daemon_arg.c_str(), (char*)nullptr);
		cerr << "Exec did not succeed! " << strerror(errno) << endl;
		throw runtime_error(string("Failure in exec: ") + strerror(errno));
	}
}

bool is_up(Service s) {
	for(auto &p : g_services) {
		if (p.second == s) {
			return true;
		}
	}
	return false;
}



void sigchld(int /*signum*/) {

	int status;
	do {
		pid_t pid;
		pid = waitpid(-1, &status, WNOHANG);
		if (pid == 0) {
			break;
		} else if (pid < 0) {
			cerr << "Error waiting: " << strerror(errno) << endl;
			break; /* I guess...*/
		} else {

			const char *s = service_2_str(g_services[pid]);
			g_services.erase(pid);
			if (WIFEXITED(status)) {
				cerr << s << " exited with status " << (int) WEXITSTATUS(status) << endl;
			} else if (WIFSIGNALED(status)) {
				cerr << s << " terminated by signal " << (int) WTERMSIG(status) << endl;
			} else if (WIFSTOPPED(status)) {
				/* this should not happen because of how the handler is installed */
				cerr << s << " stopped. Leaving stopped\n";
			} else {
				cerr << s << " did something I do not handle. Status is " << status << ". Since I am clueless, doing nothing\n";
			}
		}
	} while(true);

}

int main(int argc, char *argv[]) {
	/* why do this instead of systemd or some shit? Well, I don't know
	   what version of what this or that coinscope will be running
	   on. I was inclined to just use daemontools, which would work,
	   except that each getaddr experiment needs its own service
	   installed. I then found myself writing something that would
	   generate the services from templates that you'd then install in
	   daemontools /etc/service file. That's fine and good, but might
	   as well just have this guy do all the fork and exec stuff and
	   then the user can just install this as the service to be
	   monitored */


	bool is_tom;
	if (startup_setup(argc, argv, true, nullptr, &is_tom)) {
		return(EXIT_FAILURE);
	}

	struct sigaction sigact;
	bzero(&sigact, sizeof(sigact));
	sigact.sa_handler = sigchld;
	sigact.sa_flags = SA_NOCLDSTOP;

	if (sigaction(SIGCHLD, &sigact, nullptr) == -1) {
		cerr << "Could not install handler: " << strerror(errno) << endl;
		return EXIT_FAILURE;
	}

	vector<int> services { LOGSERVER, VERBATIM, GC + !is_tom, GETADDR };

	/* All we can do is our best, so generally we do the fork and if we
	   haven't had a SIGCHLD for a second (it will interrupt sleep)
	   then we assume it doesn't at least immediately crash and will
	   allow the next service to be started */

	do {

		for(auto &s_int : services) {
			Service s = (Service)s_int;
			while(!is_up(s)) {
				cerr << "Starting " << service_2_str(s) << endl;
				sfork(s);
				sleep(1);
			}
		}

		sleep(120); // sigchld will wake
	} while(true);

}
