#include <cstring>
#include <unistd.h>

#include <string>
#include <stack>
#include <map>
#include <stdexcept>


#include <iostream> 
#include <cstdlib>

#include "alloc_buffer.hpp"

using namespace std;


template <typename T> 
alloc_buffer<T>::alloc_buffer() : allocated_(0), buffer_(nullptr), refcount_(nullptr) {
}

template <typename T> 
alloc_buffer<T>::alloc_buffer(size_type initial_elements) 
	: allocated_(initial_elements * sizeof(POD_T)),
	  buffer_(nullptr),
	  refcount_(nullptr)
{
	if (initial_elements != 0) {
		refcount_ = new size_type(1);
		buffer_ = (POD_T*)malloc(allocated_);
		if (buffer_ == nullptr || refcount_ == nullptr) {
			delete refcount_;
			throw runtime_error(string(" failure: ") + strerror(errno));
		}
		memset(buffer_, 0, allocated_);
	}

}

template <typename T> 
alloc_buffer<T>::alloc_buffer(const alloc_buffer &copy) 
	: allocated_(copy.allocated_),
	  buffer_(copy.buffer_),
	  refcount_(copy.refcount_)
{
	if (refcount_ != nullptr) {
		++*refcount_;		
	}
}

template <typename T> 
alloc_buffer<T>::alloc_buffer(alloc_buffer &&moved) 
	:allocated_(moved.allocated_),
	 buffer_(moved.buffer_),
	 refcount_(moved.refcount_)
{
	moved.refcount_ = nullptr;
	moved.buffer_ = nullptr;
	moved.allocated_ = 0;
}

template <typename T> 
alloc_buffer<T> & alloc_buffer<T>::operator=(alloc_buffer other) {
	/* we swap with other. They'll decrement our refcount on desctruction */
	std::swap(allocated_, other.allocated_);
	std::swap(refcount_, other.refcount_);
	std::swap(buffer_, other.buffer_);
	return *this;
}

template <typename T> 
alloc_buffer<T>::~alloc_buffer() {
	if (refcount_) { /* may have been moved */
		--*refcount_;
		if (*refcount_ == 0) {
			delete refcount_;
			free(buffer_);
		}
	}
	refcount_ = nullptr;
	buffer_ = nullptr;
	allocated_ = 0;
}


template <typename T> 
void alloc_buffer<T>::realloc(size_type new_elt_cnt) {
	size_type size = new_elt_cnt * sizeof(POD_T);

	if (size == allocated_) {
		return;
	}

	if (!refcount_) {
		refcount_ = new size_type(1);
	}

	if (*refcount_ == 1) { /* yay, fast realloc */

		POD_T * newbuf = (POD_T*) ::realloc(buffer_, size);
		if (newbuf == nullptr) {
			cerr << "realloc error with size " << size << endl;
			throw std::runtime_error(string("realloc failure: ") + strerror(errno));
		}
		if (size > allocated_) {
			memset(newbuf + allocated_, 0, size - allocated_);
		}

		/* NOTE: keep iterators as offset from buffer */
		buffer_ = newbuf;
		allocated_ = size;


	} else { /* time to COW */

		alloc_buffer<T> tmp(new_elt_cnt);
		size_type n = std::min(tmp.allocated_, allocated_);
		if (n < tmp.allocated_) {
			memset(tmp.ptr() + n, 0, tmp.allocated_ - n);
		}
		memcpy(tmp.ptr(), buffer_, n);

		*this = tmp;
	}

	assert(allocated_ == size);
}


/* This acts as a write. If refcount > 1, copy made first */
template <typename T> 
typename alloc_buffer<T>::pointer alloc_buffer<T>::ptr() {
	if (!*this) {
		throw std::runtime_error("Invalid buffer");			
	} 
	if (*refcount_ > 1) {
		POD_T *newbuf = (POD_T*)malloc(allocated_);
		if (newbuf == nullptr) {
			throw runtime_error(string("realloc failure: ") + strerror(errno));
		}
		memcpy(newbuf, buffer_, allocated_);
		buffer_ = newbuf;
		--(*refcount_);
		refcount_ = new size_type(1);
	} 
	return (pointer) buffer_;
}

template class alloc_buffer<uint8_t>;
template class alloc_buffer<uint32_t>;
