
#include <cstdint>
#include <cstring>
#include <cassert>

#include <iostream>
#include <vector>
#include <memory>

#include <openssl/evp.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "crypto.hpp"

using namespace std;


struct packed_net_addr {
	uint32_t time;
	uint64_t services;
	union {
		struct {
			union {
				char bytes[16];
			} as;
		} ipv6;
		struct {
			char padding[12];
			union {
				char bytes[4];
				uint32_t number;
				struct in_addr in_addr;
			} as;
		} ipv4;
	} addr;
	uint16_t port;
} __attribute__((packed));

uint8_t get_varint_size(const uint8_t *bytes) {
	uint8_t rv(0);
	if (bytes[0] < 0xfd) {
		rv = 1;//uint8_t
	} else if (bytes[0] == 0xfd) {
		rv = 3; //uint16_t
	} else if (bytes[0] == 0xfe) {
		rv = 5; //uint32_t
	} else if (bytes[0] == 0xff) {
		rv = 9; //uint64_t
	} else {
		assert(false);
	}
	return rv;
}

uint64_t get_varint(const uint8_t *buf) {
	uint8_t size = get_varint_size(buf);
	switch(size) {
	case 1:
		return buf[0];
		break;
	case 3:
		return *((uint16_t*) (buf+1));
		break;
	case 5:
		return *((uint32_t*) (buf+1));
		break;
	case 9:
		return *((uint64_t*) (buf+1));
		break;
	default:
		assert(false);
		break;
	}
}

uint8_t to_varint(uint8_t *buf, uint64_t val) {
	uint8_t size;
	if (val < 0xfd) {
		size = 1;
		buf[0] = val;
	} else if (val <= 0xffff) {
		size = 3;
		buf[0] = 0xfd;
		*((uint16_t*) (buf+1)) = val;
	} else if (val <= 0xffffffff) {
		size = 5;
		buf[0] = 0xfe;
		*((uint32_t*) (buf+1)) = val;
	} else {
		size = 9;
		buf[0] = 0xff;
		*((uint64_t*) (buf+1)) = val;
	}
	return size;
}


int main(int argc, char *argv[]) {

	const char * test = "";
	vector<uint8_t> data;
	copy(test, test + strlen(test), back_inserter(data));
	cout << "size is " << data.size() << endl;
	unique_ptr<unsigned char[]> out(sha256(sha256(NULL, 0), 32));

	for(size_t i = 0; i < 256/8; ++i) {
		cout << hex << (unsigned int) out[i] << ' ';
	}

	cout << endl;

	uint32_t x = *((uint32_t*)out.get());


	for(size_t i = 0; i < 4; ++i) {
		cout << hex << (unsigned int) ((uint8_t *) &x)[i] << ' ';
	}

	cout << endl;



	return 0;
}
