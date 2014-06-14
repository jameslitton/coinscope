#ifndef COMMAND_STRUCTURES_HPP
#define COMMAND_STRUCTURES_HPP

/* since this is not bitcoin, integers are sent in network byte order */

#ifdef __cplusplus
namespace ctrl {
#endif

#define COMMAND_GET_CXN 1
#define COMMAND_CONNECT 2
#define COMMAND_DISCONNECT 3
#define COMMAND_GETADDR 4

struct command {
   uint32_t length;
   uint32_t command;
   /* still have to decide the format of target, as it depends on some
      data structure changes, but target will correspond to indices in
      the logs and values returned by COMMAND_GET_CXN */
   uint32_t targets[0]; 
};



#ifdef __cplusplus
};
#endif

#endif
