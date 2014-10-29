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

/* if the machine or a logger crashes, it is possible to have the file end with a length prefix but the full length bytes following. This checks that the length of the file conforms to the length prefixes, and if not, truncates the remainder */

pair<const uint8_t *,size_t> get_bytes(const char *filename) {
	int fd = open(filename, O_RDONLY);
	if (fd < 0) {
		throw runtime_error(string("bad file") + strerror(errno));
	}

	struct stat statbuf;
	if (stat(filename, &statbuf) != 0) {
		throw runtime_error(string("bad stat") + strerror(errno));
	}

	size_t mult = 0;
	size_t size = statbuf.st_size;
	size_t page_size = sysconf(_SC_PAGE_SIZE);
	while(++mult * page_size < size);

	const uint8_t * buf = (const uint8_t*)mmap(NULL, mult * page_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (buf == MAP_FAILED) {
		throw runtime_error(string("bad mmap") + strerror(errno));
	}
	return make_pair(buf, mult*page_size);
}

inline bool is_type(uint8_t t) {
	return t == DEBUG || t == CTRL || t == ERROR || t == BITCOIN || t == BITCOIN_MSG;
}

namespace po = boost::program_options;

int main(int argc, char *argv[]) {

	po::options_description desc("Options");
	desc.add_options()
		("help", "Produce help message")
		("logfile", po::value<string>(), "specify the log file");;

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

	struct stat statbuf;
	if (stat(filename.c_str(), &statbuf) != 0) {
		throw runtime_error(string("bad stat") + strerror(errno));
	}

	off_t actual = statbuf.st_size;

	int fd = open(filename.c_str(), O_RDONLY);
	if (fd < 0) {
		cerr << "Could not open file: " << strerror(errno) << endl;
	}

	off_t size(0);

	while(true) {
		uint32_t netlen;
		size_t remaining = 4;
		do { /* this loop never be necessary in practice */
			uint8_t *buf = (uint8_t*) &netlen;
			ssize_t rd = read(fd, buf + remaining - 4, remaining);
			if (rd > 0) {
				remaining -= rd;
			} else if (rd == 0) {
				break;
			} else if (rd < 0) {
				cerr << "Error reading file: " << strerror(errno) << endl;
				return EXIT_FAILURE;
			}
		} while(remaining > 0);

		if (remaining > 0) {
			/* this means we reached end of file and could not finish read */
			break;
		}


		off_t sought = lseek(fd, ntoh(netlen), SEEK_CUR);
		if (sought < 0) {
			if (errno == EINVAL) { /* probably tried to seek past end */
				break;
			} else {
				cerr << "Unexpected lseek error: " << strerror(errno) << endl;
			}
		}
		if (sought > actual) {
		  break;
		}
		size += 4 + ntoh(netlen); /* length plus bytes */
		assert(sought == size);
	}


	close(fd);

	if (size < actual) {
	  if (truncate(filename.c_str(), size) < 0) {
	    cerr << "Could not truncate: " << strerror(errno) << endl;
	  };
	}
}


