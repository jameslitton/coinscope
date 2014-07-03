#include "logger.hpp"

#include <arpa/inet.h>

using namespace std;

template <> void g_log<BITCOIN_MSG>(uint32_t id, bool is_sender, const struct bitcoin::packed_message *m) {
	cout << '[' << time(NULL) << "] BITCOIN_MSG: ";
	uint32_t net_id = hton(id);
	uint64_t net_time = hton((uint64_t)time(NULL));
	cout.write((char*)&net_id, 4);
	cout.write((char*)&net_time, 8);
	cout.write((char*)&is_sender, 1);
	cout.write((char*)m, sizeof(*m) + m->length);
	cout << endl;
}


ostream & operator<<(ostream &o, const struct ctrl::message *m) {
	o << "MSG { length => " << ntoh(m->length);
	o << ", type => " << m->message_type;
	o << ", payload => ommitted"; //o.write((char*)m, m->length + sizeof(*m));
	o << "}\n";
	return o;
}

ostream & operator<<(ostream &o, const struct ctrl::message &m) {
	return o << &m;
}


ostream & operator<<(ostream &o, const struct bitcoin::packed_message *m) {
	o << "MSG { length => " << m->length;
	o << ", magic => " << hex << m->magic;
	o << ", command => " << m->command;
	o << ", checksum => " << hex << m->command;		
	o << ", payload => ommitted"; //o.write((char*)m, m->length + sizeof(*m));
	o << "}\n";
	return o;
}

ostream & operator<<(ostream &o, const struct bitcoin::packed_message &m) {
	return o << &m;
}

ostream & operator<<(ostream &o, const struct sockaddr &addr) {
	char str[24];
	if (addr.sa_family == AF_INET) {
		const struct sockaddr_in *saddr = (struct sockaddr_in*)&addr;
		inet_ntop(addr.sa_family, &saddr->sin_addr, str, sizeof(str));
		o << str << ntoh(saddr->sin_port);
	} else if (addr.sa_family == AF_INET6) {
		const struct sockaddr_in6 *saddr = (struct sockaddr_in6*)&addr;
		inet_ntop(addr.sa_family, &saddr->sin6_addr, str, sizeof(str));
		o << str << ntoh(saddr->sin6_port);
	} else {
		cerr << "add support converting other addr types";
	}
	return o;
}

string type_to_str(enum log_type type) {
	switch(type) {
	case INTERNALS:
		return "INTERNALS";
		break;
	case CTRL:
		return "CTRL";
		break;
	case ERROR:
		return "ERROR";
		break;
	case BITCOIN:
		return "BITCOIN";
		break;
	case BITCOIN_MSG:
		return "BITCOIN_MSG";
		break;
	default:
		return "huh?";
		break;
	};
}
