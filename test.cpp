#include <cstdint>

#include <assert.h>

#include <iostream>

using namespace std;


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

int main(int argc, char *argv[]) {
   uint8_t count[] = { 0x1F};
   cout << "var int is " << get_varint(count) << endl;
   cout << "500 is " << ((unsigned int) to_varint(count, 500)) << endl;
   cout << "var int is " << get_varint(count) << endl;
   return 0;
}
