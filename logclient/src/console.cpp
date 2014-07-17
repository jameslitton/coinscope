/* standard C libraries */
#include <cstdlib>
#include <cstring>
#include <cassert>

/* standard C++ libraries */
#include <iostream>
#include <utility>
#include <iomanip>

/* standard unix libraries */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>


#include "netwrap.hpp"
#include "iobuf.hpp"
#include "logger.hpp"
#include "config.hpp"


using namespace std;

string time_to_str(const time_t *t)  {
	// return put_time(localtime(t), "%FT%T%z") !!!NOT IN G++ YET
	
	/* uncomment abouve when it is available...*/
	struct tm *tm = localtime(t);
	long offset = tm->tm_gmtoff;
	ostringstream oss;
	oss << (1900 + tm->tm_year) << '-' << setfill('0') << setw(2) << (tm->tm_mon + 1)
	    << '-' << setfill('0') << setw(2) << tm->tm_mday
	    << 'T' << setfill('0') << setw(2) << tm->tm_hour << ':' 
	    << setfill('0') << setw(2) << tm->tm_min << ':'  
	    << setfill('0') << setw(2) << tm->tm_sec;
	if (offset < 0) {
		oss << '-';
		offset = -offset;
	} else if (offset > 0) {
		oss << '+';
	} else {
		oss << 'Z';
		return oss.str();
	}

	int hours = offset / (60*60);
	offset -= hours * 60*60;

	int minutes = offset / 60;
	offset -= minutes * 60;

	int seconds = offset;

	oss << setfill('0') << setw(2) << hours << ':'
	    << setfill('0') << setw(2) << minutes << ':' << setfill('0') << setw(2) << seconds;
	return oss.str();
}


void print_message(iobuf &input_buf) {
	uint8_t *buf = input_buf.raw_buffer();
	enum log_type lt(static_cast<log_type>(buf[0]));
	time_t time = ntoh(*( (uint64_t*)(buf+1)));
	
	uint8_t *msg = buf + 8 + 1;

	cout << time_to_str(&time) << ' ' << type_to_str(lt);

	if (lt == BITCOIN_MSG) {
		cout << " ID:" << ntoh(*((uint32_t*) msg)) << " IS_SENDER:" << *((bool*) (msg+4));
		msg += 5;
		cout << " " << ((const struct bitcoin::packed_message*)(msg)) << endl;
	} else {
		cout << " " << ((char*)msg) << endl;
	}
}

int main(int argc, char *argv[]) {
	if (argc == 2) {
		load_config(argv[1]);
	} else {
		load_config("../netmine.cfg");
	}

	const libconfig::Config *cfg(get_config());

	string root((const char*)cfg->lookup("logger.root"));

	/* TODO: make configurable */
	mkdir(root.c_str(), 0777);
	string client_dir(root + "clients/");

	int client = unix_sock_client(client_dir + "all", false);

	bool reading_len(true);
	uint32_t to_read(sizeof(uint32_t));

	iobuf input_buf;

	while(true) {
		input_buf.grow(to_read);
		ssize_t r = read(client, input_buf.offset_buffer(), to_read);
		if (r > 0) {
			input_buf.seek(input_buf.location() + r);
			to_read -= r;
		} else if (r == 0) {
			cerr << "Disconnected\n";
			return EXIT_SUCCESS;
		} else if (r < 0) {
			cerr << "Got error, " << strerror(errno) << endl;
			return EXIT_FAILURE;
		}

		if (to_read == 0) {
			if (reading_len) {
				uint32_t netlen = *((uint32_t*)input_buf.raw_buffer());
				to_read = ntoh(netlen);
				input_buf.seek(0);
				reading_len = false;
			} else {
				print_message(input_buf);
				input_buf.seek(0);
				to_read = 4;
				reading_len = true;
			}
		}
	}

}
