#include <cassert>

#include <unistd.h>
#include <arpa/inet.h>
#include <random>

#include "netwrap.hpp"
#include "logger.hpp"



using namespace std;


log_buffer *g_log_buffer;

uint32_t log_buffer::fixed_id = ~0;

const static size_t store_size(4096);

size_t g_log_cursor(0);
wrapped_buffer<uint8_t> g_log_store(store_size);


void log_watcher(ev::timer &w, int /*revents*/) {
	if (g_log_buffer == nullptr) {
		try {
			g_log_buffer = new log_buffer(unix_sock_client(*static_cast<string*>(w.data), true));
		} catch(const network_error &e) {
			g_log_buffer = nullptr;
			g_log<ERROR>(e.what());
		}
	}
}


log_buffer::log_buffer(int fd) : write_queue(), fd(fd), io(), id_netorder(0) { 
	io.set<log_buffer, &log_buffer::io_cb>(this);
	io.set(fd, ev::WRITE);
	io.start();

	if (fixed_id == (uint32_t)~0) {
		std::random_device rd;
		std::mt19937 gen(rd());

		/* if this collides with another connector, that's "bad" but
		   also very unlikely. If ground control is running, he should
		   spike those kids and get them to reassign */

		std::uniform_int_distribution<uint32_t> dis(0, std::numeric_limits<uint32_t>::max());

	}

	uint32_t id = fixed_id;
	std::cerr << "acquiring source id " << id << endl;

	id_netorder = hton(id);

}

void log_buffer::append(wrapped_buffer<uint8_t> &ptr, size_t len) {
	size_t to_write = write_queue.to_write();
	write_queue.append(ptr, len);
	if (to_write == 0) {
		io.set(fd, ev::WRITE);
	}
}

void log_buffer::io_cb(ev::io &watcher, int /*revents*/) {
	ssize_t r(1);
	while(write_queue.to_write() && r > 0) {
		auto res = write_queue.do_write(watcher.fd);
		r = res.first;
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
	}
	if (write_queue.to_write() == 0) {
		io.set(watcher.fd, ev::NONE);
	}
}
log_buffer::~log_buffer() {
	io.stop();
	close(io.fd);
}

static void append_buf(wrapped_buffer<uint8_t> &buf, size_t len) {
	if (g_log_buffer) {
		g_log_buffer->append(buf, len);
	} else {
		std::cerr << "<<CONSOLE FALLBACK>> " << "BITCOIN: " << " TODO: pretty print this fallback, but really, don't use the fallback\n";
	}
}


template <> void g_log<BITCOIN>(uint32_t update_type, uint32_t handle_id, const struct sockaddr_in &remote, 
                                const struct sockaddr_in &local, const char * text, uint32_t text_len) {
	uint64_t net_time = hton((uint64_t)ev::now(ev_default_loop()));

	size_t len = 1 + sizeof(net_time) + sizeof(handle_id) + sizeof(update_type) +
		2*sizeof(remote) + sizeof(text_len) + text_len + 4 /* sid */;

	if (store_size == 1 || len + 4 > g_log_store.allocated() - g_log_cursor) {
		if (g_log_cursor > 0) { /* yes, may conceivably just want to grow buffer for sufficiently small cursors... */
			append_buf(g_log_store, g_log_cursor);
		}
		g_log_cursor = 0;
		g_log_store = wrapped_buffer<uint8_t>(max((size_t)store_size, len+4));
	}

	uint8_t *base_ptr = g_log_store.ptr() + g_log_cursor;
	uint8_t *cur_ptr = base_ptr;


	uint32_t netlen = hton((uint32_t)len);
	copy((uint8_t*) &netlen, ((uint8_t*)&netlen) + 4, cur_ptr);
	cur_ptr += 4;

	if (g_log_buffer) {
		std::copy((uint8_t*)& (g_log_buffer->id_netorder),
		          (uint8_t*)(&(g_log_buffer->id_netorder)) + (sizeof(g_log_buffer->id_netorder)), cur_ptr);
	} else {
		uint8_t buffer[sizeof(g_log_buffer->id_netorder)] = {0};
		std::copy(buffer, buffer + sizeof(g_log_buffer->id_netorder), cur_ptr);
	}
	cur_ptr += sizeof(g_log_buffer->id_netorder);


	uint8_t typ = BITCOIN;
	copy((uint8_t*) &typ, ((uint8_t*) &typ) + 1, cur_ptr);
	cur_ptr += 1;

	copy((uint8_t*) &net_time, ((uint8_t*)&net_time) + sizeof(net_time),
	     cur_ptr);
	cur_ptr += sizeof(net_time);

	handle_id = hton(handle_id);
	copy((uint8_t*) &handle_id, ((uint8_t*)&handle_id) + sizeof(handle_id), 
	     cur_ptr);
	cur_ptr += sizeof(handle_id);

	update_type = hton(update_type);
	copy((uint8_t*) &update_type, ((uint8_t*)&update_type) + sizeof(update_type), 
	     cur_ptr);
	cur_ptr += sizeof(update_type);

	copy((uint8_t*) &remote, ((uint8_t*)&remote) + sizeof(remote), 
	     cur_ptr);
	cur_ptr += sizeof(remote);

	copy((uint8_t*) &local, ((uint8_t*)&local) + sizeof(local), 
	     cur_ptr);
	cur_ptr += sizeof(local);

	uint32_t net_txtlen = hton((uint32_t)text_len);
	copy((uint8_t*) &net_txtlen, ((uint8_t*)&net_txtlen) + sizeof(net_txtlen), 
	     cur_ptr);
	cur_ptr += sizeof(net_txtlen);

	if (text_len) {
		copy((uint8_t*)text, (uint8_t*)text + text_len, cur_ptr);
		cur_ptr += text_len;
	}

	g_log_cursor += cur_ptr - base_ptr;
}

template <> void g_log<BITCOIN_MSG>(uint32_t id, bool is_sender, const struct bitcoin::packed_message *m) {

	uint64_t net_time = hton((uint64_t)ev::now(ev_default_loop()));
	uint32_t net_id = hton(id);
	size_t len = 1 + sizeof(net_time) + sizeof(net_id) + 1 + sizeof(*m) + m->length + 4 /* sid */;

	if (len + 4 > g_log_store.allocated() - g_log_cursor) {
		if (g_log_cursor > 0) {
			append_buf(g_log_store, g_log_cursor);
		}
		g_log_cursor = 0;
		g_log_store = wrapped_buffer<uint8_t>(max((size_t)store_size, len+4));
	}

	uint8_t *base_ptr = g_log_store.ptr() + g_log_cursor;
	uint8_t *cur_ptr = base_ptr;

	uint32_t netlen = hton((uint32_t)len);
	copy((uint8_t*) &netlen, ((uint8_t*)&netlen) + 4, cur_ptr);
	cur_ptr += 4;


	if (g_log_buffer) {
		std::copy((uint8_t*)& (g_log_buffer->id_netorder),
		          (uint8_t*)(&(g_log_buffer->id_netorder)) + (sizeof(g_log_buffer->id_netorder)), cur_ptr);
	} else {
		uint8_t buffer[sizeof(g_log_buffer->id_netorder)] = {0};
		std::copy(buffer, buffer + sizeof(g_log_buffer->id_netorder), cur_ptr);
	}
	cur_ptr += sizeof(g_log_buffer->id_netorder);

	uint8_t typ = BITCOIN_MSG;
	copy((uint8_t*) &typ, ((uint8_t*) &typ) + 1, cur_ptr);
	cur_ptr += 1;

	copy((uint8_t*) &net_time, ((uint8_t*)&net_time) + sizeof(net_time),
	     cur_ptr);
	cur_ptr += sizeof(net_time);

	copy((uint8_t*) &net_id, ((uint8_t*)&net_id) + sizeof(net_id), 
	     cur_ptr);
	cur_ptr += sizeof(net_id);

	assert(sizeof(is_sender) == 1);
	copy((uint8_t*) &is_sender, ((uint8_t*)&is_sender) + sizeof(is_sender), 
	     cur_ptr);
	cur_ptr += sizeof(is_sender);

	copy((uint8_t*) m, ((uint8_t*)m) + sizeof(*m) + m->length, 
	     cur_ptr);
	cur_ptr += sizeof(*m) + m->length;

	g_log_cursor += cur_ptr - base_ptr;

}


ostream & operator<<(ostream &o, const struct ctrl::message *m) {
	o << "MSG { length => " << ntoh(m->length);
	o << ", type => " << (int) m->message_type;
	o << ", payload => ommitted"; //o.write((char*)m, m->length + sizeof(*m));
	o << "}";
	return o;
}

ostream & operator<<(ostream &o, const struct ctrl::message &m) {
	return o << &m;
}


ostream & operator<<(ostream &o, const struct bitcoin::packed_message *m) {
	o << "MSG { length => " << m->length;
	o << ", magic => 0x" << hex << m->magic;
	o << ", command => " << m->command;
	o << ", checksum => 0x" << hex << m->checksum;		
	o << ", payload => ommitted"; //o.write((char*)m, m->length + sizeof(*m));
	o << "}";
	o << dec;
	return o;
}

ostream & operator<<(ostream &o, const struct bitcoin::packed_message &m) {
	return o << &m;
}

ostream & operator<<(ostream &o, const struct sockaddr &addr) {
	char str[24];
	if (addr.sa_family == AF_INET) {
		const struct sockaddr_in *saddr = (const struct sockaddr_in*)&addr;
		inet_ntop(addr.sa_family, &saddr->sin_addr, str, sizeof(str));
		o << str << ':' << ntoh(saddr->sin_port);
	} else if (addr.sa_family == AF_INET6) {
		const struct sockaddr_in6 *saddr = (const struct sockaddr_in6*)&addr;
		inet_ntop(addr.sa_family, &saddr->sin6_addr, str, sizeof(str));
		o << str << ':' << ntoh(saddr->sin6_port);
	} else {
		o << str << "unsupported addr family: " << addr.sa_family;
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
	case CONNECTOR:
		return "CONNECTOR";
		break;
	case CLIENT:
		return "CLIENT";
		break;
	case GROUND_CTRL:
		return "GROUND_CTRL";
		break;
	default:
		return "UNKNOWN";
		break;
	};
}
