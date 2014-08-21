#ifndef WRAPPED_BUFFER_HPP
#define WRAPPED_BUFFER_HPP

#include "mmap_buffer.hpp"
#include "alloc_buffer.hpp"

/* this uses alloc_buffer for small allocations and switched to an
   mmap_buffer once allocations reach a certain size. Both are COW */

/* it is important to note, this is a BUFFER, not a container. It can
   be used as an allocator for containers. placement new/delete is not
   called on objects. Itertor end() spans to the end of the allocated
   space, not necessarily the end of the used space, though it does
   work to maintain alignment so it can be used as an allocator */

template <typename T>
class wrapped_buffer {
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
	mmap_buffer<T> *mbuffer_;
	alloc_buffer<T> *abuffer_;


	struct iterator { /* non-const iterator has to COW, so blech */
		//pos is in elt strides 

		typedef typename wrapped_buffer<T>::difference_type difference_type;
		typedef typename wrapped_buffer<T>::value_type value_type;
		typedef typename wrapped_buffer<T>::pointer pointer;
		typedef typename wrapped_buffer<T>::reference reference;
		typedef typename std::random_access_iterator_tag iterator_category;

		iterator(wrapped_buffer *owner, size_type pos) : owner_(owner), pos_(pos) {
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
		wrapped_buffer *owner_;
		size_type pos_;
	};


public:

	wrapped_buffer();
	wrapped_buffer(size_type initial_allocation);
	wrapped_buffer(const wrapped_buffer &copy);
	wrapped_buffer(wrapped_buffer &&moved);
	wrapped_buffer & operator=(wrapped_buffer other);

	explicit operator bool() const {
		return mbuffer_ || abuffer_;
	}

	~wrapped_buffer();
	iterator begin() {
		return iterator(this, 0);
	}

	iterator end() {
		return iterator(this, allocated() / sizeof(POD_T));
	}

	const_iterator cbegin() const {
		if (*this) {
			return mbuffer_ ? mbuffer_->cbegin() : abuffer_->cbegin();
		} else {
			throw std::runtime_error("Invalid buffer");
		}
	}

	const_iterator cend() const {
		if (*this) {
			return mbuffer_ ? mbuffer_->cend() : abuffer_->cend();
		} else {
			throw std::runtime_error("Invalid buffer");
		}

	}

	void realloc(size_type new_elt_cnt);

	/* this doesn't act as a write, no copy made */
	const_pointer const_ptr() const {
		if (*this) {
			return mbuffer_ ? mbuffer_->const_ptr() : abuffer_->const_ptr();
		} else {
			throw std::runtime_error("Invalid buffer");
		}

	}

	pointer ptr();

	size_type allocated() const {
		if (*this) {
			return mbuffer_ ? mbuffer_->allocated() : abuffer_->allocated();
		} else {
			return 0;
		}
	}

	long use_count() const { 
		if (*this) {
			return mbuffer_ ? mbuffer_->use_count() : abuffer_->use_count();			
		} else {
			return 0; 
		}
	}

};

#endif
