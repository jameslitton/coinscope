#include "logger.hpp"

#include <arpa/inet.h>

using namespace std;


ostream & operator<<(ostream &o, const struct ctrl::message *m) {
	/* need to standardize on an output format */
	o << "CMSG";
	o.write((const char*) m, m->length);
	return o;
}

ostream & operator<<(ostream &o, const struct ctrl::message &m) {
	return o << &m;
}


ostream & operator<<(ostream &o, const struct bitcoin::packed_message *m) {
	/* need to standardize on an output format */
	o << "PMSG";
	o.write((const char*) m, m->length);
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

ostream & logger::operator()(enum log_type type) {
	return cout << type_to_str(type) << ": ";
}
ostream & logger::operator()(enum log_type type, uint32_t id) {
	return cout << type_to_str(type) << "(" << id << "): ";
}
ostream & logger::operator()(enum log_type type, uint32_t id, bool sender) { /* type has to be BITCOIN_MSG, sender is true if we sent */
	return cout << type_to_str(type) << "(" << id << ", " << sender << "): ";
}

logger g_log;
