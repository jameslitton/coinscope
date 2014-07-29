#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

#include <string>
#include <stack>
#include <map>
#include <stdexcept>


#include <cstdlib>

#include "alloc_buffer.hpp"


#include <iostream>

using namespace std;


template <typename T> 
alloc_buffer<T>::alloc_buffer() : alloc_buffer(0) {}

template <typename T> 
alloc_buffer<T>::alloc_buffer(size_type initial_elements) 
	: allocated_(initial_elements * sizeof(POD_T)),
	  buffer_(nullptr),
	  refcount_(new size_type(1))
{

	buffer_ = (POD_T*)malloc(allocated_);
	if (buffer_ == nullptr) {
		throw runtime_error(string(" failure: ") + strerror(errno));
	}
}

template <typename T> 
alloc_buffer<T>::alloc_buffer(alloc_buffer &copy) 
	: allocated_(copy.allocated_),
	  buffer_(copy.buffer_),
	  refcount_(copy.refcount_)
{
	++*refcount_;
	
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

/*
  alloc_buffer & operator=(alloc_buffer && other) {
  swap(allocated_, other.allocated_);
  swap(refcount_, other.refcount_);
  swap(buffer_, other.buffer_);
  return *this;
  }
*/

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

	if (*refcount_ == 1) { /* yay, fast realloc */

		POD_T * newbuf = (POD_T*) ::realloc(buffer_, size);
		if (newbuf == nullptr) {
			throw std::runtime_error(string("realloc failure: ") + strerror(errno));
		}
		/* NOTE: keep iterators as offset from buffer */
		buffer_ = newbuf;
		allocated_ = size;


	} else { /* time to COW */

		alloc_buffer<T> tmp(size);
		size_type n = std::min(new_elt_cnt * sizeof(POD_T), allocated_);
		memcpy(tmp.ptr(), buffer_, n);
		*this = tmp;
	}

}


/* This acts as a write. If refcount > 1, copy made first */
template <typename T> 
typename alloc_buffer<T>::pointer alloc_buffer<T>::ptr() {
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



