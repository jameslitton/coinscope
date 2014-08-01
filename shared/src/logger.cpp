#include <cassert>

#include <arpa/inet.h>

#include "logger.hpp"



using namespace std;

log_buffer::log_buffer(int fd) : write_queue(), to_write(), fd(fd), io() { 
	io.set<log_buffer, &log_buffer::io_cb>(this);
	io.set(fd, ev::WRITE);
	io.start();
}
void log_buffer::append(cvector<uint8_t> &&ptr) {
	/* TODO: make no copy */
	uint32_t netlen = hton((uint32_t)ptr.size());
	size_t old_loc = write_queue.location();
	write_queue.seek(write_queue.location() + to_write);
	iobuf_spec::append(&write_queue, (uint8_t*)&netlen, sizeof(netlen));
	write_queue.seek(write_queue.location() + sizeof(netlen));
	iobuf_spec::append(&write_queue, ptr.data(), ptr.size());
	write_queue.seek(old_loc);
	if (to_write == 0) {
		io.set(fd, ev::WRITE);
	}
	to_write += ptr.size() + sizeof(netlen);

}
void log_buffer::io_cb(ev::io &watcher, int /*revents*/) {
	ssize_t r(1);
	while(to_write && r > 0) {
		assert(write_queue.location() + to_write <= write_queue.end());
		r = write(watcher.fd, write_queue.offset_buffer(), to_write);
		if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) { 
			/* where to log when the log is dead... */
			cerr << "Cannot write out log: " << strerror(errno) << endl;
			io.stop();
			close(io.fd);
			g_log_buffer = NULL;
			delete this;
			/* TODO: re-establish connection? */
			return;
		}
		if (r > 0) {
			to_write -= r;
			write_queue.seek(write_queue.location() + r);
		}
	}
	if (to_write == 0) {
		io.set(watcher.fd, ev::NONE);
		write_queue.seek(0);
	}
}
log_buffer::~log_buffer() {
	io.stop();
	close(io.fd);
}

log_buffer *g_log_buffer;



template <> void g_log<BITCOIN_MSG>(uint32_t id, bool is_sender, const struct bitcoin::packed_message *m) {

	/* TODO: eliminate copy */
	uint64_t net_time = hton((uint64_t)time(NULL));
	uint32_t net_id = hton(id);
	cvector<uint8_t> ptr(1 + sizeof(net_time) + sizeof(net_id) + 1 + sizeof(*m) + m->length);
	ptr.push_back(BITCOIN_MSG);
	auto back = back_inserter(ptr);
	copy((uint8_t*) &net_time, ((uint8_t*)&net_time) + sizeof(net_time),
	     back);
	copy((uint8_t*) &net_id, ((uint8_t*)&net_id) + sizeof(net_id), 
	     back);
	copy((uint8_t*) &is_sender, ((uint8_t*)&is_sender) + sizeof(is_sender), 
	     back);
	copy((uint8_t*) m, ((uint8_t*)m) + sizeof(*m) + m->length, 
	     back);
	g_log_buffer->append(move(ptr));
}


ostream & operator<<(ostream &o, const struct ctrl::message *m) {
	o << "MSG { length => " << ntoh(m->length);
	o << ", type => " << m->message_type;
	o << ", payload => ommitted"; //o.write((char*)m, m->length + sizeof(*m));
	o << "}";
	return o;
}

ostream & operator<<(ostream &o, const struct ctrl::message &m) {
	return o << &m;
}


ostream & operator<<(ostream &o, const struct bitcoin::packed_message *m) {
	o << "MSG { length => " << m->length;
	o << ", magic => " << hex << m->magic;
	o << ", command => " << m->command;
	o << ", checksum => " << hex << m->checksum;		
	o << ", payload => ommitted"; //o.write((char*)m, m->length + sizeof(*m));
	o << "}";
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
		o << str << ':' << ntoh(saddr->sin_port);
	} else if (addr.sa_family == AF_INET6) {
		const struct sockaddr_in6 *saddr = (struct sockaddr_in6*)&addr;
		inet_ntop(addr.sa_family, &saddr->sin6_addr, str, sizeof(str));
		o << str << ':' << ntoh(saddr->sin6_port);
	} else {
		cerr << "add support converting other addr types";
	}
	return o;
}

string type_to_str(enum log_type type) {
	switch(type) {
	case DEBUG:
		return "DEBUG";
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
