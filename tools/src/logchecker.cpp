/* standard C libraries */
#include <cstdlib>
#include <cstring>
#include <cassert>

/* standard C++ libraries */
#include <iostream>
#include <iomanip>
#include <utility>
#include <map>
#include <stack>
#include <memory>
#include <set>

/* standard unix libraries */
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

/* external libraries */
#include <boost/program_options.hpp>

#include "network.hpp"
#include "command_structures.hpp"
#include "bitcoin.hpp"
#include "logger.hpp"

using namespace std;

namespace po = boost::program_options;

uint32_t validate_record(const wrapped_buffer<uint8_t> &buf) {

	static uint64_t last_timestamp(0);


	uint32_t rv = 0;
	const struct log_format *log( (const struct log_format*) buf.const_ptr());
	switch(log->type) {
	case DEBUG: case CTRL: case ERROR: case BITCOIN: case BITCOIN_MSG: case CONNECTOR:
		break;
	default:
		cerr << "Invalid log type, got " << log->type << endl;
		rv = 1;
	}

	uint64_t timestamp = ntoh(log->timestamp) + 120; /* allow two minute jitter. Unlikely to happen in a corrupt log */
	if (last_timestamp && (timestamp < last_timestamp  || timestamp - last_timestamp > 86300*3)) {
		cerr << "timestamp negative or jumped more than three days. Got " << (timestamp - 120) << " last was " << last_timestamp << endl;
		rv |= 2;
	}

	last_timestamp = timestamp - 120;

	if (log->type == BITCOIN_MSG) {
		const struct bitcoin_msg_log_format *msg((const struct bitcoin_msg_log_format*) log);
		const char *s;
		for(s = msg->msg.command; *s && isprint(*s); ++s);
		if (*s) {
			cerr << "Got invalid command: " << msg->msg.command << endl;
			rv |= 4;
		}
	}
	return rv;
}

int main(int argc, char *argv[]) {

	int rv(0);

	po::options_description desc("Options");
	desc.add_options()
		("help", "Produce help message")
		("logfile", po::value<string>(), "specify the log file");


	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);

   if (vm.count("help")) {
      cout << desc << endl;
      return 1;
   }

	if (vm.count("logfile") != 1) {
		cerr << "Need to specify a logfile" << endl;
	}

	string filename = vm["logfile"].as<string>();

	int fd = open(filename.c_str(), O_RDONLY);
	if (fd < 0) {
		cerr << "Could not open file " << filename << ' ' << strerror(errno) << endl;
		exit(1);
	}



	uint32_t remaining = 4;
	uint32_t cursor(0);
	bool reading_len = true;
	wrapped_buffer<uint8_t> buf(remaining);



	for(;;) {

		do { /* this loop never be necessary in practice */
			ssize_t rd = read(fd, buf.ptr() + cursor, remaining);
			if (rd > 0) {
				remaining -= rd;
				cursor += rd;
			} else if (rd == 0) {
				break;
			} else if (rd < 0) {
				cerr << "Error reading file: " << strerror(errno) << endl;
				return EXIT_FAILURE;
			}
		} while(remaining > 0);

		if (remaining > 0) {
			/* this means we reached end of file and had remaining bytes to read. This is not an error for the importer, so allowing it */
			cerr << "This file seems to have dangling data. If you are concerned about this, run logtruncate on it." << endl;
			break;
		}

		if (reading_len) {
			uint32_t netlen = *((const uint32_t*) buf.const_ptr());
			remaining = ntoh(netlen);
			cursor = 0;
			buf.realloc(remaining);
			reading_len = false;
		} else {
			if (validate_record(buf) & (1|2)) {
				rv = 1;
				break;
			}
			cursor = 0;

			reading_len = true;
			remaining = 4;
		}


		
	}
	return rv;
}

