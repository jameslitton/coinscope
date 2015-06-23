#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <libconfig.h++>

int startup_setup(int argc, char *argv[], bool drop_perms = true, int *instance = NULL, bool *is_tom = NULL); /* refactor this */
const libconfig::Config * load_config(const char *filename);
const libconfig::Config * get_config();

#endif
