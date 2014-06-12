/* standard C libraries */
#include <cstdlib>
#include <cstring>
#include <cassert>


/* standard C++ libraries */
#include <vector>
#include <random>
#include <iostream>

/* standard unix libraries */
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

/* third party libraries */
#include <ev++.h>

/* our libraries */
#include "bitcoin.hpp"

using namespace std;

namespace bc = bitcoin;

const uint32_t RECV_MASK = 0x0000ffff; // all receive flags should be in the mask 
const uint32_t RECV_HEADER = 0x1;
const uint32_t RECV_PAYLOAD = 0x2;
const uint32_t RECV_VERSION_INIT = 0x40; /* we initiated the handshake */
const uint32_t RECV_VERSION_REPLY = 0x80;

const uint32_t SEND_MASK = 0xffff0000;
const uint32_t SEND_MESSAGE = 0x10;
const uint32_t SEND_VERSION_INIT = 0x20; /* we initiated the handshake */
const uint32_t SEND_VERSION_REPLY = 0x40;
   
const size_t BUFSZ = 4096;

void handle_message() { assert(false); }

/* all event data goes into here. It is FIFO. Must optimize*/
class iobuf {
public:
	void * offset_buffer() {
      assert(buffer);
		return buffer.get() + loc;
	}

   void * raw_buffer() {
      assert(buffer);
      return buffer.get();
   }

   size_t location() const { return loc; }

   void seek(size_t new_loc) { 
      assert(new_loc < allocated);
      loc = new_loc;
   }

   void reserve(size_t x) {
      if (x > allocated) {
         unique_ptr<uint8_t[]> tmp(new uint8_t[x]);
         copy(buffer.get(), buffer.get() + allocated, tmp.get());
         buffer = move(tmp);
         allocated = x;
      }
   }

   void shrink(size_t x) {
      if (x < allocated) {
         unique_ptr<uint8_t[]> tmp(new uint8_t[x]);
         copy(buffer.get(), buffer.get() + x, tmp.get());
         buffer = move(tmp);
         allocated = x;
      }
   }
protected:
	unique_ptr<uint8_t[]> buffer;
   size_t allocated;
   size_t loc;
};

class handler {
private:
   iobuf read_queue; /* application needs to read and act on this data */
   size_t to_read;

   iobuf write_queue; /* application wants this written out across network */
   size_t to_write;

	uint32_t state;

public:
	handler(uint32_t a_state = SEND_VERSION_INIT) : state(a_state) {
		
	};
   
   void io_cb(ev::io &watcher, int revents) {
      if ((state & RECV_MASK) && (revents & ev::READ)) {
         ssize_t r(1);
         while(r > 0) { /* do all reads we can in this event handler */
            do {
               r = read(watcher.fd, read_queue.offset_buffer(), to_read);
               if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN) { cerr << strerror(errno) << endl; }
               if (r > 0) {
                  to_read -= r;
                  read_queue.seek(read_queue.location() + r);
               }
            } while (r > 0 && to_read > 0);

            if (to_read == 0) {
               /* item needs to be handled */
               switch(state & RECV_MASK) {
               case RECV_HEADER:
                  /* interpret data as message header and get length, reset remaining */ 
                  to_read = ((struct bc::packed_message*) read_queue.raw_buffer())->length;
                  if (to_read == 0) { /* payload is packed message */
                     handle_message();
                     read_queue.seek(0);
                     to_read = sizeof(struct bc::packed_message);
                  } else {
                     read_queue.reserve(sizeof(struct bc::packed_message) + to_read);
                     state = (state & ~RECV_MASK) | RECV_PAYLOAD;
                  }
                  break;
               case RECV_PAYLOAD:
                  handle_message(); /* must be able to handle pongs as well */
                  read_queue.seek(0);
                  to_read = sizeof(struct bc::packed_message);
                  state = (state & ~RECV_MASK) | RECV_HEADER;
                  break;
               case RECV_VERSION_INIT: // we initiated handshake, we expect ack
                  // next message should be zero length header with verack command
                  state = (state & SEND_MASK) | RECV_HEADER; 
                  break;
               case RECV_VERSION_REPLY: // they initiated handshake, send our version and verack
                  state = (state & SEND_MASK) | SEND_VERSION_REPLY | RECV_HEADER;
                  break;
               }

            }
         }
         
      }

      if (revents & ev::WRITE) {

         ssize_t r(1);
	      while (to_write && r > 0) {
		      r = write(watcher.fd, write_queue.offset_buffer(), to_write);
            if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN) { cerr << strerror(errno) << endl; }
		      if (r > 0) {
			      write_queue.seek(write_queue.location() + r);
		      }
	      }

         if (to_write == 0) {
            switch(state & SEND_MASK) {
            case SEND_VERSION_INIT:
               /* set to fire watch read events */
               state = RECV_VERSION_INIT;
               break;
            default:
               /* we actually do no special handling here so we can
                  buffer up writes. Beyond SEND_VERSION_INIT, we don't
                  care why we are sending */
               break;
            }

            /* unregister write event! */
            state &= ~SEND_MASK;
            write_queue.seek(0);


         }
         

      }

   }
};
   


void do_parent(vector<int> &fds) {
   ev::default_loop loop;
   //myclass obj;
   vector<ev::io *> watchers;

   ev::timer timer;
   //timer.set<myclass, &myclass::timer_cb>(&obj);
   timer.set(5, 1);
   timer.start();

   for(auto it = fds.cbegin(); it != fds.cend(); ++it) {
      ev::io *iow = new ev::io();
      //iow->set<myclass, &myclass::io_cb>(&obj);
      iow->set(*it, ev::READ);
      iow->start();
      watchers.push_back(iow);
   }

   fds.clear();

   while(true) {
      loop.run();
      cerr << "here\n";
   }
   

}


int main(int argc, const char *argv[]) {
   
   return EXIT_SUCCESS;
}
