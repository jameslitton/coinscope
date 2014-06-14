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
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

/* third party libraries */
#include <ev++.h>

/* our libraries */
#include "bitcoin.hpp"
#include "iobuf.hpp"

using namespace std;

namespace bc = bitcoin;

const uint32_t RECV_MASK = 0x0000ffff; // all receive flags should be in the mask 
const uint32_t RECV_HEADER = 0x1;
const uint32_t RECV_PAYLOAD = 0x2;
const uint32_t RECV_VERSION_INIT = 0x4; /* we initiated the handshake */
const uint32_t RECV_VERSION_REPLY = 0x8;

const uint32_t SEND_MASK = 0xffff0000;
const uint32_t SEND_MESSAGE = 0x10000;
const uint32_t SEND_VERSION_INIT = 0x20000; /* we initiated the handshake */
const uint32_t SEND_VERSION_REPLY = 0x40000;
   
const size_t BUFSZ = 4096;

const string USER_AGENT("specify version string");


class handler {
private:
   iobuf read_queue; /* application needs to read and act on this data */
   size_t to_read;

   iobuf write_queue; /* application wants this written out across network */
   size_t to_write;

   in_addr remote_addr;
   uint16_t remote_port;

   in_addr this_addr; /* address we connected on */
   uint16_t this_port;

	uint32_t state;

public:
	handler(uint32_t a_state = SEND_VERSION_INIT) : state(a_state) {
		
	};

   void handle_message_recv(const struct bc::packed_message *msg) { 
      cout << "RMSG " << inet_ntoa(remote_addr) << ' ' << msg->command << ' ' << msg->length << endl;
      if (strcmp(msg->command, "ping") == 0) {
         write_queue.append(msg);
      }
   }


   
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
                     handle_message_recv((struct bc::packed_message*) read_queue.raw_buffer());
                     read_queue.seek(0);
                     to_read = sizeof(struct bc::packed_message);
                  } else {
                     read_queue.reserve(sizeof(struct bc::packed_message) + to_read);
                     state = (state & SEND_MASK) | RECV_PAYLOAD;
                  }
                  break;
               case RECV_PAYLOAD:
                  /* must be able to handle pongs as well */
                  handle_message_recv((struct bc::packed_message*) read_queue.raw_buffer());
                  read_queue.seek(0);
                  to_read = sizeof(struct bc::packed_message);
                  state = (state & SEND_MASK) | RECV_HEADER;
                  break;
               case RECV_VERSION_INIT: // we initiated handshake, we expect ack
                  // next message should be zero length header with verack command
                  state = (state & SEND_MASK) | RECV_HEADER; 
                  break;
               case RECV_VERSION_REPLY: // they initiated handshake, send our version and verack
                  struct bc::combined_version vers(bc::get_version(USER_AGENT, remote_addr, remote_port, this_addr, this_port));
                  write_queue.append(&vers);
                  unique_ptr<struct bc::packed_message, void(*)(void*)> msg(bc::get_message("verack"));
                  write_queue.append(msg.get());
                  if (!(state & SEND_MASK)) {
                     /* add to write event */
                  }
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
