#ifndef COLLECTOR_HPP
#define COLLECTOR_HPP

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <deque>


class collector {
public:
	struct wrapped_data {
		std::unique_ptr<uint8_t[]> data;
		size_t len;
		wrapped_data(std::unique_ptr<uint8_t[]> m_data, size_t m_len) 
		: data(nullptr), len(m_len) { data.swap(m_data); }
	};

	void append(std::unique_ptr<uint8_t[]> data, size_t len);
	std::shared_ptr<wrapped_data> pop(uint32_t id);
	uint32_t add_consumer(); /* adds a consumer and returns an id. */
	void retire_consumer(uint32_t);
	static collector * get() {
		static std::unique_ptr<collector> c(new collector());
		return c.get();
	}
private:
	collector() {}
	uint32_t idpool;
	std::unordered_map<uint32_t, 
	                   std::deque<std::shared_ptr<wrapped_data> > > queues;
};

#endif
