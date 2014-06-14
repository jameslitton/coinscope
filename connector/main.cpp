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
