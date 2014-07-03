#include <iostream>

#include "config.hpp"

using namespace libconfig;
using namespace std;

static Config g_config;


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
