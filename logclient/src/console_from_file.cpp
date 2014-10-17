/* standard C libraries */
#include <cstdlib>
#include <cstring>
#include <cassert>

/* standard C++ libraries */
#include <iostream>
#include <utility>
#include <iomanip>
#include <fstream>

/* standard unix libraries */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

#include "netwrap.hpp"
#include "logger.hpp"

const size_t PAGE_SIZE = 4096;
const size_t MULT = 10000;

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


void print_message(const uint8_t *buf, size_t len) {
	(void) len;
	const struct log_format *log = (const struct log_format*) buf;
	enum log_type lt(static_cast<log_type>(log->type));
	time_t time = ntoh(log->timestamp);
	
	const uint8_t *msg = log->rest;

	cout << time_to_str(&time);
	cout << " (" << ntoh(log->source_id) << ") ";
	cout << type_to_str(lt);

	if (lt == BITCOIN_MSG) {
		cout << " ID:" << ntoh(*((uint32_t*) msg)) << " IS_SENDER:" << *((bool*) (msg+4));
		msg += 5;
		cout << " " << ((const struct bitcoin::packed_message*)(msg)) << endl;
	} else if (lt == BITCOIN) {
		/* TODO: write a function to unwrap this as a struct */
		uint32_t update_type = ntoh(*((uint32_t*)(msg + 4)));
		const struct sockaddr *remote = (const sockaddr*)(msg + 8);
		const struct sockaddr *local = (const sockaddr*)(msg + 8 + sizeof(sockaddr_in));
		uint32_t text_len = ntoh(*(uint32_t*)(msg + 8 + sizeof(sockaddr_in)*2));
		const char * text = (char*)( msg + 8 + sizeof(sockaddr_in)*2 + 4);

		cout << " ID:" << ntoh(*((uint32_t*) msg)) << ", UPDATE_TYPE: ";
		switch (update_type) {
		case CONNECT_SUCCESS:
			cout << "CONNECT_SUCCESS";
			break;
		case ACCEPT_SUCCESS:
			cout << "ACCEPT_SUCCESS";
			break;
		case ORDERLY_DISCONNECT:
			cout << "ORDERLY_DISCONNECT";
			break;
		case WRITE_DISCONNECT:
			cout << "WRITE_DISCONNECT";
			break;
		case UNEXPECTED_ERROR:
			cout << "UNEXPECTED_ERROR";
			break;
		case CONNECT_FAILURE:
			cout << "CONNECT_FAILURE";
			break;
		case PEER_RESET:
			cout << "PEER_RESET";
			break;
		case CONNECTOR_DISCONNECT:
			cout << "CONNECTOR_DISCONNECT";
			break;
		default:
			cout << "Unknown update type(" << update_type << ")";
			break;
		}

		cout << ", REMOTE: " << *remote << ", local: " << *local;
		if (text_len) {
			cout << ", text: " << text << endl;
		} else {
			cout << endl;
		}
	} else {
		cout << " " << ((char*)msg) << endl;
	}
}

void grow_buf(uint8_t **buf, uint32_t sz) {
	*buf = (uint8_t *) realloc((void *) *buf, sz);
}

int main(int argc, char *argv[]) {
	if(argc != 2) {
		cerr << "Syntax: " << argv[0] << " logname" << endl;
		exit(1);
	}
	//
	string logfile(argv[1]);
	int read_fd = open(logfile.c_str(), O_RDONLY);
	if(read_fd < 0) {
		cerr << "Failed to open file descriptor for " << logfile << endl;
		exit(1);
	}

	bool reading_len(true);

	uint32_t to_read = 0;
	uint32_t bytes_read = 0;
	
	uint32_t buf_size = PAGE_SIZE * MULT;
	uint8_t *buf = (uint8_t *) malloc(buf_size);

	uint32_t netlen;

	while(true) {
		if (reading_len) {
			if(read(read_fd,buf,4) != (ssize_t) 4) {
				cerr << "Hit end at partial length" << endl;
				break;
			}
			netlen = *((const uint32_t*) buf);
			to_read = ntoh(netlen);
			reading_len = false;
		} else {
			if(to_read > buf_size) {
				buf_size = ((to_read * 2) / PAGE_SIZE + 1) * PAGE_SIZE;
				grow_buf(&buf, buf_size);
			}
			bytes_read=read(read_fd,buf,to_read);
			if(bytes_read != to_read) {
				cerr << "Hit end at partial message" << endl;
				break;
			};
			print_message(buf, (size_t) bytes_read);
			reading_len = true;
		}
	}
}

