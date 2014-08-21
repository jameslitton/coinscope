#ifndef READ_BUFFER_HPP
#define READ_BUFFER_HPP

#include "wrapped_buffer.hpp"

/* to be used for accumulating read calls and extract realloc when ready to act on the data */
class read_buffer { 
public:
	/* return value from read, whether the read is complete (i.e., buffer can be extracted) */
	read_buffer(size_t to_read) : cursor_(0), to_read_(to_read), buffer_(to_read_) {
	}
	std::pair<int,bool> do_read(int fd); /* will read to_read_ bytes */
	std::pair<int,bool> do_read(int fd, size_t size); /* will read size bytes */
   void to_read(size_t);
   size_t to_read() const;
	void cursor(size_t loc) ;
	size_t cursor() const;
	bool hungry() const;
	wrapped_buffer<uint8_t> extract_buffer();
	/* Doesn't work for some reason :-( TODO: figure out why
	operator const uint8_t*() const { return buffer_.const_ptr(); }
	operator uint8_t*()  { return buffer_.ptr(); }
	*/
private:
	size_t cursor_;
	size_t to_read_;
	wrapped_buffer<uint8_t> buffer_;

};

#endif
