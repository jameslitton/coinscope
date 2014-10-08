#include <iostream>

#include <boost/program_options.hpp>

#include "config.hpp"

using namespace libconfig;
using namespace std;

static Config g_config;

namespace po = boost::program_options;

int startup_setup(int argc, char * argv[]) {
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
