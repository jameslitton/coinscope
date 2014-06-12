#include "bitcoin.hpp"

#include <cassert>

#include <string>

using namespace std;

namespace bitcoin {



uint8_t get_varint_size(const uint8_t *bytes) {
   uint8_t rv(0);
   if (bytes[0] < 0xfd) {
      rv = 1;//uint8_t
   } else if (bytes[0] == 0xfd) {
      rv = 3; //uint16_t
   } else if (bytes[0] == 0xfe) {
      rv = 5; //uint32_t
   } else if (bytes[0] == 0xff) {
      rv = 9; //uint64_t
   } else {
      assert(false);
   }
   return rv;
}

uint64_t get_varint(const uint8_t *buf) {
   uint8_t size = get_varint_size(buf);
   switch(size) {
   case 1:
      return buf[0];
      break;
   case 3:
      return *((uint16_t*) (buf+1));
      break;
   case 5:
      return *((uint32_t*) (buf+1));
      break;
   case 9:
      return *((uint64_t*) (buf+1));
      break;
   default:
      assert(false);
      break;
   }
}

uint8_t to_varint(uint8_t *buf, uint64_t val) {
   uint8_t size;
   if (val < 0xfd) {
      size = 1;
      buf[0] = val;
   } else if (val <= 0xffff) {
      size = 3;
      buf[0] = 0xfd;
      *((uint16_t*) (buf+1)) = val;
   } else if (val <= 0xffffffff) {
      size = 5;
      buf[0] = 0xfe;
      *((uint32_t*) (buf+1)) = val;
   } else {
      size = 9;
      buf[0] = 0xff;
      *((uint64_t*) (buf+1)) = val;
   }
   return size;
}

struct combined_version get_version(const string &user_agent) {
   uint8_t buf[9];
   uint8_t size = to_varint(buf, user_agent.length());
   struct combined_version rv(size + user_agent.length());
   rv.version() = MAX_VERSION;
   rv.services() = SERVICES;
   rv.timestamp() = time(NULL);
   //rv.addr_recv() = x; /* target address */
   //fill_my_address(&rv.addr_from());
   //rv.nonce() = y; /* random value */

   /* copy user agent...gross */
   copy(buf, buf + size, rv.user_agent());
   copy(user_agent.cbegin(), user_agent.cend(), rv.user_agent() + size);

   rv.start_height() = 0;
   rv.relay() = true;
   return rv;
}

};

