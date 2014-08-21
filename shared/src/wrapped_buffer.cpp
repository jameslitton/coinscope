#include <cstring>
#include <unistd.h>

#include <string>
#include <stack>
#include <map>
#include <stdexcept>

#include <cstdlib>

#include "wrapped_buffer.hpp"

using namespace std;

#define MMAP_THRESHOLD 50000

template <typename T> 
wrapped_buffer<T>::wrapped_buffer() : mbuffer_(nullptr), abuffer_(nullptr) {}

template <typename T> 
wrapped_buffer<T>::wrapped_buffer(size_type initial_elements) 
	: mbuffer_(nullptr),
	  abuffer_(nullptr)
{
	if (initial_elements != 0) {
		size_t size = initial_elements * sizeof(POD_T);
		if (size > MMAP_THRESHOLD) {
			mbuffer_ = new mmap_buffer<T>(initial_elements);
		} else {
			abuffer_ = new alloc_buffer<T>(initial_elements);
		}
		if (mbuffer_ == nullptr && abuffer_ == nullptr) {
			throw runtime_error(string("wrapped_buffer failure: ") + strerror(errno));
		}
	}
}

template <typename T> 
wrapped_buffer<T>::wrapped_buffer(const wrapped_buffer &copy) 
	: mbuffer_(nullptr), abuffer_(nullptr)
{
	if (copy.mbuffer_) {
		mbuffer_ = new mmap_buffer<T>(*copy.mbuffer_);
	} else if (copy.abuffer_) {
		abuffer_ = new alloc_buffer<T>(*copy.abuffer_);
	} 
}

template <typename T> 
wrapped_buffer<T>::wrapped_buffer(wrapped_buffer &&moved) 
	: mbuffer_(moved.mbuffer_),
	  abuffer_(moved.abuffer_)
{
	moved.mbuffer_ = nullptr;
	moved.abuffer_ = nullptr;
}

template <typename T> 
wrapped_buffer<T> & wrapped_buffer<T>::operator=(wrapped_buffer other) {
	/* we swap with other. They'll decrement our refcount on desctruction */
	std::swap(mbuffer_, other.mbuffer_);
	std::swap(abuffer_, other.abuffer_);
	return *this;
}

template <typename T> 
wrapped_buffer<T>::~wrapped_buffer() {
	delete mbuffer_;
	delete abuffer_;
}


template <typename T> 
void wrapped_buffer<T>::realloc(size_type new_elt_cnt) {
	size_type size = new_elt_cnt * sizeof(POD_T);

	if (size > MMAP_THRESHOLD) {

		if (mbuffer_) {
			mbuffer_->realloc(new_elt_cnt);
		} else if (abuffer_) {
			mbuffer_ = new mmap_buffer<T>(new_elt_cnt);
			size_t to_copy = min(mbuffer_->allocated(), abuffer_->allocated());
			memcpy(mbuffer_->ptr(), abuffer_->cbegin(), to_copy);
			delete abuffer_;
			abuffer_ = nullptr;
		} else {
			mbuffer_ = new mmap_buffer<T>(new_elt_cnt);
		}

	} else {

		if (mbuffer_) {
			abuffer_ = new alloc_buffer<T>(new_elt_cnt);
			size_t to_copy = min(mbuffer_->allocated(), abuffer_->allocated());
			memcpy(abuffer_->ptr(), mbuffer_->cbegin(), to_copy);
			delete mbuffer_;
			mbuffer_ = nullptr;
		} else if (abuffer_){
			abuffer_->realloc(new_elt_cnt);
		} else {
			abuffer_ = new alloc_buffer<T>(new_elt_cnt);
		}
	}


}

template <typename T> 
typename wrapped_buffer<T>::pointer wrapped_buffer<T>::ptr() {
	if (*this) {
		return mbuffer_ ? mbuffer_->ptr() : abuffer_->ptr();
	} else {
		throw runtime_error("bad buffer");
	}
}

template class wrapped_buffer<uint8_t>;
template class wrapped_buffer<uint32_t>;
