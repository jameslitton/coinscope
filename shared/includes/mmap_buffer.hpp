#ifndef MMAP_BUFFER_HPP
#define MMAP_BUFFER_HPP

#include <cassert>
#include <cstdint>

#include <stdexcept>
#include <iterator>
#include <type_traits>



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
	mutable size_type * refcount_;

public:



	struct iterator { /* non-const iterator has to COW, so blech */
		//pos is in elt strides 

		typedef typename mmap_buffer<T>::difference_type difference_type;
		typedef typename mmap_buffer<T>::value_type value_type;
		typedef typename mmap_buffer<T>::pointer pointer;
		typedef typename mmap_buffer<T>::reference reference;
		typedef typename std::random_access_iterator_tag iterator_category;

		iterator(mmap_buffer *owner, size_type pos) : owner_(owner), pos_(pos) {
			if (!*owner) {
				throw std::invalid_argument("bad owner");
			}
		}

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

		bool operator==(const iterator &other) const {
			/* two iterators that share the same underlying buffer but have different owners are considered different */
			return other.owner_ == owner_ &&
				other.pos_ == pos_;
		}

		bool operator !=(const iterator &other) const {
			return !(other == *this);
		}

	private:
		mmap_buffer *owner_;
		size_type pos_;
		
	};


	mmap_buffer();
	mmap_buffer(size_type initial_allocation);

	mmap_buffer(const mmap_buffer &copy);
	mmap_buffer(mmap_buffer &&moved);
	mmap_buffer & operator=(mmap_buffer other);


	explicit operator bool() const {
		return buffer_;
	}

	~mmap_buffer();

	iterator begin() {
		return iterator(this, 0);
	}

	iterator end() {
		return iterator(this, allocated_ / sizeof(POD_T));
	}

	const_iterator cbegin() const {
		if (*this) {
			return (const_pointer)buffer_;
		} else {
			throw std::runtime_error("Invalid buffer");
		}
	}

	const_iterator cend() const {
		if (*this) {
			return (const_pointer)(buffer_ + (allocated_ / sizeof(POD_T)));
		} else {
			throw std::runtime_error("Invalid buffer");
		}
	}

	void realloc(size_type new_elt_cnt);

	/* this doesn't act as a write, no copy made */
	const_pointer const_ptr() const {
		if (*this) {
			return (const_pointer)(buffer_);
		} else {
			throw std::runtime_error("Invalid buffer");
		}
	}

	/* This acts as a write. If refcount > 1, copy made first */
	pointer ptr();

	size_type allocated() const {
		return allocated_;
	}

	long use_count() const { return refcount_ ? *refcount_ : 0; }


};


#endif
