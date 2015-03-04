#include <iostream>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <boost/program_options.hpp>

#include "config.hpp"

using namespace libconfig;
using namespace std;

static Config g_config;

namespace po = boost::program_options;

int drop_perms(const char *username, const char *group) {
	struct passwd *pwd = getpwnam(username);
	if (pwd == nullptr) {
		cerr << "Username does not exist\n";
		return 4;
	}

	struct group *grp = getgrnam(group);
	if (grp == nullptr) {
		cerr << "Group does not exist\n";
		return 4;
	}

	uid_t uid = pwd->pw_uid;
	gid_t gid = grp->gr_gid;

	if (getgid() != gid) {
		if (setgid(gid) < 0) {
			cerr << "Could not setgid: " << strerror(errno) << endl;
			return 8;
		}
	}

	if (getuid() != uid) {
		if (setuid(uid) < 0) {
			cerr << "Could not setuid: " << strerror(errno) << endl;
			return 8;
		}
	}

	umask(2);

	return 0;
}

int startup_setup(int argc, char * argv[], bool do_perms) {
   po::options_description desc("Options");
   desc.add_options()
      ("help", "Produce help message")
      ("configfile", po::value<string>()->default_value("../netmine.cfg"), "specify the config file")
      ("daemonize", po::value<int>()->default_value(0), "daemonize");

   po::variables_map pvm;
   po::store(po::parse_command_line(argc, argv, desc), pvm);
   po::notify(pvm);

   if (pvm.count("help")) {
      cout << desc << endl;
      return 1;
   }

   load_config(pvm["configfile"].as<string>().c_str());
   if (do_perms) {
	   int err = drop_perms((const char*) g_config.lookup("permissions.username"),
	                        (const char*) g_config.lookup("permissions.group"));
	   if (err) {
		   return err;
	   }
   }
   

   if (pvm["daemonize"].as<int>() == 1) {
      if (daemon(0, 0)) {
         cerr << "Failed to daemonize: " << strerror(errno) << endl;
         return 2;
      }
   }
   return 0;
}

const Config * load_config(const char *filename) {
	try {
		g_config.readFile(filename);
	} catch (FileIOException &e) {
		cerr << "Error reading file: " << e.what() << endl;
		throw e;
	} catch(ParseException &e) {
		cerr << "Parsing Error: " << e.getError() << " on " << e.getFile() 
		     << ":" << e.getLine() << endl;
		throw e;
	} 
	return &g_config;
}
const Config * get_config() {
	return &g_config;
}
