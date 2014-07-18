#ifndef MMAP_BUFFER_HPP
#define MMAP_BUFFER_HPP

#include <cstring>
#include <cassert>
#include <cstdint>
#include <sys/mman.h>
#include <unistd.h>

#include <string>
#include <stdexcept>


/* it is important to note, this is a BUFFER, not a container. It can
   be used as an allocator for containers. placement new/delete is not
   called on objects. Itertor end() spans to the end of the allocated
   space, not necessarily the end of the used space, though it does
   work to maintain alignment so it can be used as an allocator */

/* all allocations are rounded up to be divisible by page_size */

template <typename T>
class mmap_buffer { /* this is an efficient COW buffer using mmap */
public:
	typedef typename std::aligned_storage<sizeof(T),alignof(T)>::type POD_T;
	typedef T value_type;
	typedef typename std::size_t size_type;
	typedef typename std::ptrdiff_t difference_type;
	typedef value_type& reference;
	typedef const value_type& const_reference;
	typedef T* pointer;
	typedef const T* const_pointer;
	typedef const_pointer const_iterator;


private:
	size_type allocated_;
	POD_T * buffer_;
	size_type * refcount_;

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

public:



	struct iterator { /* non-const iterator has to COW, so blech */
		//pos is in elt strides 

		typedef typename mmap_buffer<T>::difference_type difference_type;
		typedef typename mmap_buffer<T>::value_type value_type;
		typedef typename mmap_buffer<T>::pointer pointer;
		typedef typename mmap_buffer<T>::reference reference;
		typedef typename std::random_access_iterator_tag iterator_category;

		iterator(mmap_buffer *owner, size_type pos) : owner_(owner), pos_(pos) {}

		iterator & operator++() { /* prefix inc */
			++pos_;
			return *this;
		}

		iterator operator+(const iterator &x) {
			return iterator(owner_, pos_ + x.pos_);
		}

		iterator operator+(size_type x) {
			return iterator(owner_, pos_ + x);
		}

		iterator operator-(size_type x) {
			return iterator(owner_, pos_ - x);
		}

		difference_type operator-(const iterator &x) {
			difference_type lhs(pos_);
			difference_type rhs(x.pos_); 
			return lhs - rhs;
		}

		iterator operator++(int) { //postfix
			iterator rv(*this);
			++(*this);
			return rv;
		}

		iterator & operator--() { /* prefix dec */
			assert(pos_ > 0);
			--pos_;
			return *this;
		}

		iterator operator--(int) {
			iterator rv(*this);
			--(*this);
			return rv;
		}

		value_type & operator*() const {
			return *(pointer)(owner_->ptr() + pos_);
		}

		value_type* operator->() const {
			return (pointer)(owner_->ptr() + pos_);
			
		}

		bool operator==(const iterator &other) {
			/* two iterators that share the same underlying buffer but have different owners are considered different */
			return other.owner_ == owner_ &&
				other.pos_ == pos_;
		}

		bool operator !=(const iterator &other) {
			return !(other == *this);
		}

	private:
		mmap_buffer *owner_;
		size_type pos_;
		
	};


	mmap_buffer(size_type initial_elements) 
		: allocated_(round_to_page(initial_elements * sizeof(POD_T))),
		  buffer_(nullptr),
		  refcount_(new size_type(1))
	{
		buffer_ = (POD_T*)mmap(NULL, allocated_, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		if (buffer_ == MAP_FAILED) {
			throw std::runtime_error(std::string("mmap failure: ") + strerror(errno));
		}
	}

	mmap_buffer(mmap_buffer &copy) 
		: allocated_(copy.allocated_),
		  buffer_(copy.buffer_),
		  refcount_(copy.refcount_)
	{
		++*refcount_;
	
	}

	mmap_buffer(mmap_buffer &&moved) 
		:allocated_(moved.allocated_),
		 buffer_(moved.buffer_),
		 refcount_(moved.refcount_)
	{
		moved.refcount = moved.buffer_ = nullptr;
		moved.allocated_ = 0;

	}

	mmap_buffer & operator=(mmap_buffer other) {
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

	~mmap_buffer() {
		if (refcount_) { /* may have been moved */
			--*refcount_;
			if (*refcount_ == 0) {
				delete refcount_;
				munmap(buffer_, allocated_);
			}
		}
		refcount_ = nullptr;
		buffer_ = nullptr;
		allocated_ = 0;
	}

	iterator begin() {
		return iterator(this, 0);
	}

	iterator end() {
		return iterator(this, allocated_ / sizeof(POD_T));
	}

	const_iterator cbegin() const {
		return (const_pointer)buffer_;
	}

	const_iterator cend() const {
		return (const_pointer)(buffer_ + (allocated_ / sizeof(POD_T)));
	}

	void realloc(size_type new_elt_cnt) {
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



	/* this doesn't act as a write, no copy made */
	const_pointer const_ptr() const {
		return const_cast<const_pointer>(buffer_);
	}

	/* This acts as a write. If refcount > 1, copy made first */
	pointer ptr() {
		if (*refcount_ > 1) {
			POD_T *newbuf = (POD_T*)mmap(NULL, allocated_, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
			if (newbuf == MAP_FAILED) {
				throw std::runtime_error(std::string("mmap failure: ") + strerror(errno));
			}
			memcpy(newbuf, buffer_, allocated_);
			buffer_ = newbuf;
			--(*refcount_);
			refcount_ = new size_type(1);
		} 
		return (pointer) buffer_;
	}

	size_type allocated() const {
		return allocated_;
	}

};

#endif
