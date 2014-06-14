#include "command_handler.hpp"

#include <iostream>

#include <unistd.h>
#include <arpa/inet.h>

using namespace std;


namespace ctrl {

void handler::handle_message_recv(const struct command *msg) { 
   /* do control command handler crap */
}


void handler::io_cb(ev::io &watcher, int revents) {
   if ((state & RECV_MASK) && (revents & ev::READ)) {
      ssize_t r(1);
      while(r > 0) { 
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
               to_read = ((struct command*) read_queue.raw_buffer())->length;
               if (to_read == 0) { /* payload is packed message */
                  handle_message_recv((struct command*) read_queue.raw_buffer());
                  read_queue.seek(0);
                  to_read = sizeof(struct command);
               } else {
                  read_queue.reserve(sizeof(struct command) + to_read);
                  state = (state & SEND_MASK) | RECV_PAYLOAD;
               }
               break;
            case RECV_PAYLOAD:
               handle_message_recv((struct command*) read_queue.raw_buffer());
               read_queue.seek(0);
               to_read = sizeof(struct command);
               state = (state & SEND_MASK) | RECV_HEADER;
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
         /* unregister write event! */
         state &= ~SEND_MASK;
         write_queue.seek(0);

      }
   }
}


};

