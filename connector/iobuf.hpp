#ifndef IOBUF_HPP
#define IOBUF_HPP

#include <algorithm>

#include "bitcoin.hpp"

class iobuf;

namespace iobuf_spec {

void append(iobuf *, const uint8_t *ptr, size_t len);

template <typename T> void append(iobuf *buf, const T *ptr) {
   append(buf, (const uint8_t*) ptr, sizeof(*ptr));
}
template <> void append<bitcoin::packed_message>(iobuf *buf, const bitcoin::packed_message *ptr);

};

/* all event data goes into here. It is FIFO. Must optimize*/
class iobuf {
public:
	uint8_t * offset_buffer();
   uint8_t * raw_buffer();
   size_t location() const;

   template <typename T>
   void append(const T *ptr) {
      iobuf_spec::append<T>(this, ptr);
   }

   void seek(size_t new_loc);

   void reserve(size_t x);
   void shrink(size_t x);
protected:
   std::unique_ptr<uint8_t[]> buffer;
   size_t allocated;
   size_t loc;
};


#endif
