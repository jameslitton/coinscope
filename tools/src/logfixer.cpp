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

#include "network.hpp"
#include "command_structures.hpp"
#include "bitcoin.hpp"
#include "logger.hpp"
#include "../../clients/lib.hpp"

using namespace std;

/* there WAS a bug with verbatim where length prefixes weren't
   printed. You probably do not need this file. Committing for
   posterity/adaptability for potential future problems */

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
	return t == DEBUG || t == CTRL || t == ERROR || t == BITCOIN || t == BITCOIN_MSG || t == CONNECTOR || t == CLIENT;
}

int main() {

	int out = open("fixed_verbatim.out", O_WRONLY | O_TRUNC | O_CREAT, 0777);
	if (out < 0) {
		throw runtime_error(strerror(errno));
	}
	pair<const uint8_t*,size_t> meta(get_bytes("verbatim.out"));
	const uint8_t *buf = meta.first;
	
	const uint8_t *last = nullptr;
	time_t last_time(0);
	while(buf < meta.first + meta.second) { /* assume it starts good */
		/* find candidate types, if what follows looks like a timestamp, assume it is */
		while(!is_type(*buf)) { ++buf; }
		time_t time = ntoh(*( (uint64_t*)(buf+1)));

		if (last == nullptr || ((time - last_time >= 0) && (time - last_time < 5*60))) {

			if (last != nullptr) {
				uint32_t len = buf-last;
				len = hton(len);
				/* write len:[last,buf) */
				do_write(out, &len, 4);
				do_write(out, last, buf - last);
			}
			last = buf;
			last_time = time;
		}

		++buf;
	}

	close(out);
}


