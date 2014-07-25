#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

#include <string>
#include <stack>
#include <map>
#include <stdexcept>


#include "mmap_buffer.hpp"



using namespace std;

static map<size_t, stack<void*> > mmap_old_pages;

static size_t round_to_page(size_t size) {
	static long page_size = sysconf(_SC_PAGESIZE);
	assert(page_size > 0);

	size_t lower = size / page_size;
	if (lower*page_size < size) {
		size = (lower+1)*page_size;
	}

	/* don't allow zero allocations */
	return size ? size : page_size;
	
}




template <typename T> 
mmap_buffer<T>::mmap_buffer() : mmap_buffer(0) {}

template <typename T> 
mmap_buffer<T>::mmap_buffer(size_type initial_elements) 
	: allocated_(round_to_page(initial_elements * sizeof(POD_T))),
	  buffer_(nullptr),
	  refcount_(new size_type(1))
{

	auto it = mmap_old_pages.find(allocated_);
	if (it != mmap_old_pages.end() && it->second.size()) {
		buffer_ = (POD_T*)it->second.top();
		it->second.pop();
	} else {
		buffer_ = (POD_T*)mmap(NULL, allocated_, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		if (buffer_ == MAP_FAILED) {
			throw std::runtime_error(std::string("mmap failure: ") + strerror(errno));
		}
	}
}

template <typename T> 
mmap_buffer<T>::mmap_buffer(mmap_buffer &copy) 
	: allocated_(copy.allocated_),
	  buffer_(copy.buffer_),
	  refcount_(copy.refcount_)
{
	++*refcount_;
	
}

template <typename T> 
mmap_buffer<T>::mmap_buffer(mmap_buffer &&moved) 
	:allocated_(moved.allocated_),
	 buffer_(moved.buffer_),
	 refcount_(moved.refcount_)
{
	moved.refcount_ = nullptr;
	moved.buffer_ = nullptr;
	moved.allocated_ = 0;

}

template <typename T> 
mmap_buffer<T> & mmap_buffer<T>::operator=(mmap_buffer other) {
	/* we swap with other. They'll decrement our refcount on desctruction */
	std::swap(allocated_, other.allocated_);
	std::swap(refcount_, other.refcount_);
	std::swap(buffer_, other.buffer_);
	return *this;
}

/*
  mmap_buffer & operator=(mmap_buffer && other) {
  swap(allocated_, other.allocated_);
  swap(refcount_, other.refcount_);
  swap(buffer_, other.buffer_);
  return *this;
  }
*/

template <typename T> 
mmap_buffer<T>::~mmap_buffer() {
	if (refcount_) { /* may have been moved */
		--*refcount_;
		if (*refcount_ == 0) {
			delete refcount_;
			auto it = mmap_old_pages.find(allocated_);
			if (it != mmap_old_pages.end() && it->second.size() * allocated_ < (1 << 20)) {
				it->second.push(buffer_);
			} else {
				munmap(buffer_, allocated_);
			}
		}
	}
	refcount_ = nullptr;
	buffer_ = nullptr;
	allocated_ = 0;
}


template <typename T> 
void mmap_buffer<T>::realloc(size_type new_elt_cnt) {
	size_type size = round_to_page(new_elt_cnt * sizeof(POD_T));

	if (size == allocated_) {
		return;
	}

	if (*refcount_ == 1) { /* yay, fast realloc */

		POD_T *newbuf = (POD_T*) mremap(buffer_, allocated_, size, MREMAP_MAYMOVE);
		if (newbuf == MAP_FAILED) {
			throw std::runtime_error(std::string("mremap failure: ") + strerror(errno));
		}
		/* NOTE: keep iterators as offset from buffer */
		buffer_ = newbuf;


	} else { /* time to COW */
		mmap_buffer<T> tmp(size);
		size_type n = std::min(new_elt_cnt * sizeof(POD_T), allocated_);
		memcpy(tmp.ptr(), buffer_, n);
		*this = tmp;
	}
}


/* This acts as a write. If refcount > 1, copy made first */
template <typename T> 
typename mmap_buffer<T>::pointer mmap_buffer<T>::ptr() {
	if (*refcount_ > 1) {

		auto it = mmap_old_pages.find(allocated_);
		POD_T *newbuf = nullptr;
		if (it != mmap_old_pages.end() && it->second.size()) {
			newbuf = (POD_T*)it->second.top();
			it->second.pop();
		} else {
			newbuf = (POD_T*)mmap(NULL, allocated_, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
			if (newbuf == MAP_FAILED) {
				throw std::runtime_error(std::string("mmap failure: ") + strerror(errno));
			}
		}
		memcpy(newbuf, buffer_, allocated_);
		buffer_ = newbuf;
		--(*refcount_);
		refcount_ = new size_type(1);
	} 
	return (pointer) buffer_;
}

template class mmap_buffer<uint8_t>;



