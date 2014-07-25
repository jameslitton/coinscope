#ifndef CVECTOR_HPP
#define CVECTOR_HPP

#include <limits>
#include <memory>
#include <stdexcept>
#include <algorithm>


/* This is essentially a vector implementation that is modeled to be a
   little bit safer/easier when intermixed with C
   interfaces. Essentially allocating and getting direct use of
   managed vector memory is what is allowed. Intended with guys that
   don't need to be constructed and destroyed. Any POD would be okay,
   but currently it forces integral types */

#include "boost/concept_check.hpp"

template <class T, class Allocator = std::allocator<T> >
class cvector {
public:
	typedef T value_type;
	typedef Allocator allocator_type;
	typedef typename std::size_t size_type;
	typedef typename std::ptrdiff_t difference_type;
	typedef value_type& reference;
	typedef const value_type& const_reference;
	typedef typename std::allocator_traits<Allocator>::pointer pointer;
	typedef typename std::allocator_traits<Allocator>::const_pointer const_pointer;
	typedef pointer iterator;
	typedef const_pointer const_iterator;
	/* would have to define classes for the next two...*/
	//typedef typename reverse_iterator;
	//typedef typename const_reverse_iterator;

	BOOST_CONCEPT_ASSERT((boost::Integer<T>));

	cvector(size_type initial_capacity=16) 
		: container(new T[initial_capacity]),
		  allocated(initial_capacity),
		  size_(0) {}

	cvector(T *to_copy, size_type elements) 
		: container(new T[elements]),
		  allocated(size),
		  size_(size) {
		std::copy(to_copy, to_copy + elements, begin());
	}

	cvector(const cvector<T,Allocator> &other)
		: container(new T[other.allocated]),
		  allocated(other.allocated),
		  size_(other.size_) {
		std::copy(other.begin(), other.end(), begin());
	}

	cvector(cvector<T,Allocator> &&other)
		: container(std::move(other.container)),
		  allocated(other.allocated),
		  size_(other.size_) 
	{
		other.allocated = 0;
		other.size_ = 0;
	}

	~cvector() {
		/* unique_ptr handles it */
	}

	cvector & operator=(cvector<T,Allocator> other) {
		this->swap(other);
		return *this;
	}

	cvector & operator=(cvector<T,Allocator> &&other) {
		container = std::move(other.container);
		size_ = other.size_;
		allocated = other.allocated;
		other.allocated = other.size_ = 0;
	}
		          

	reference operator[](size_type pos) {
		return container[pos];
	}

	constexpr const_reference operator[](size_type pos) const {
		return container[pos];
	}

	reference front() { return container[0]; }
	const_reference front() const { return container[0]; }

	reference back() { 
		if (size_ > 0) {
			return container[size_-1]; 
		}
		throw std::out_of_range("Invalid operation with size zero");
	}

	const_reference back() const {
		if (size_ > 0) {
			return container[size_-1]; 
		}
		throw std::out_of_range("Invalid operation with size zero");
	}
		
	pointer data() { return container.get(); }
	const_pointer data() const { return container.get(); }
	iterator begin() { return container.get(); }
	const_iterator begin() const { return container.get(); }
	const_iterator cbegin() const { return container.get(); }

	iterator end() { return container.get() + size_; }
	const_iterator end() const { return container.get() + size_; } 
	const_iterator cend() const { return container.get() + size_; }

	bool empty() const { return size_; }
	size_type size() const { return size_; }
	size_type max_size() const { return std::numeric_limits<size_type>::max(); }

	void reserve( size_type new_cap ) {
		if (new_cap > allocated) {
			std::unique_ptr<T[]> tmp(new T[new_cap]);
			std::copy(cbegin(), cend(), tmp.get());
			allocated = new_cap;
			container.swap(tmp);
		}
	}

	size_type capacity() const { return allocated; }
	void shrink_to_fit() {
		if (allocated > size_) {
			std::unique_ptr<T[]> tmp(new T[size_]);
			copy(tmp.get(), cbegin(), cend());
			allocated = size_;
			container.swap(tmp);
		}
	}

	void clear() { size_ = 0; } /* note, no destructors called, hence the concept check*/
	/* don't have insert */
	/* don't have emplace (why would you) */

	iterator erase(const_iterator first, const_iterator last ) {
		iterator rv(end());
		if (last == end()) {
			size_ = first - begin();
		} else {
			for(const_iterator cur = first; cur < last; ++cur) {
				*cur = *(++cur);
				rv = cur + 1;
				--size_;
			}
		}
		return rv;
	}

	iterator erase( const_iterator pos ) {
		return erase(pos, pos+1);
	}

	void push_back( const T& value ) {
		if (!container) {
			container = std::unique_ptr<T[]>(new T[16]);
			allocated = 16;
		}
		if (size_ == allocated) {
			reserve(allocated * 2);
		}
		container[size_++] = value;
	}
	

	void pop_back() { --size_; }

	void lazy_resize(size_type count) {
		/* this is the same as resize, but doesn't initialize the new values */
		if (count > size_) {
			reserve(count);
		}
		size_ = count;
	}

	void resize( size_type count, const value_type& value) {
		if (count > size_) {
			reserve(count);
			iterator cur = end();
			size_ = count; /* changes value of end() */
			for(; cur != end(); ++cur) {
				*cur = value;
			}
		} else {
			size_ = count;
		}
	}

	void resize( size_type count) {
		if (count > size_) {
			resize(count, T());
		} else {
			size_ = count;
		}
	}
		
	void swap( cvector<T,Allocator>& other) {
		other.container.swap(container);
		std::swap(other.allocated, allocated);
		std::swap(other.size_, size_);
	}
	
private:
	std::unique_ptr<T[]> container;
	size_type allocated;
	size_type size_;
};

#endif
