#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <libconfig.h++>


const libconfig::Config * load_config(const char *filename);
const libconfig::Config * get_config();

#endif
