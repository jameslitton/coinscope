/* standard C libraries */
#include <cstdlib>
#include <cstring>
#include <cassert>

/* standard C++ libraries */
#include <iostream>
#include <utility>
#include <map>
#include <stack>
#include <memory>
#include <set>
#include <mutex>
#include <condition_variable>
#include <thread>

/* standard unix libraries */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>


#include "network.hpp"
#include "command_structures.hpp"
#include "bitcoin.hpp"
#include "config.hpp"
#include "logger.hpp"
#include "lib.hpp"

/* get all existing connections. Write result out to mmapped file (for spider, for instance) and standard out */

using namespace std;
using namespace ctrl;


int main(int argc, char *argv[]) {

	if (argc == 2) {
		load_config(argv[1]);
	} else {
		load_config("../netmine.cfg");
	}

	const libconfig::Config *cfg(get_config());

	int sock = unix_sock_client((const char*)cfg->lookup("connector.control_path"), false);

	struct sockaddr_in *data(nullptr);
	size_t data_len(0);

	std::function<std::function<void(struct connection_info *, size_t)>(size_t) > callgen(
		        [&](size_t addresses) -> std::function<void(struct connection_info*, size_t)> {
			        if (addresses) {
				        size_t mult = 0;
				        size_t page_size = sysconf(_SC_PAGE_SIZE);

				        while(++mult * page_size < addresses * sizeof(struct sockaddr_in));


				        char tmpname[] = "/tmp/nodelist-XXXXXX";
				        int fd = mkstemp(tmpname);
				        if (fd < 0) {
					        throw runtime_error(strerror(errno));
				        }

				        if (ftruncate(fd, mult * page_size) != 0) {
					        throw runtime_error(strerror(errno));
				        }
				        data = (struct sockaddr_in*) mmap(NULL, mult * page_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
				        close(fd);
				        if (data == MAP_FAILED) {
					        throw runtime_error(strerror(errno));
				        }
				        data_len = mult * page_size;
				        return [data](struct connection_info *info, size_t elt) {
					        cout << "handle_id: " << ntoh(info->handle_id) << endl;
					        cout << "remote: " << *((struct sockaddr*)&info->remote_addr) << endl;
					        cout << "local: " << *((struct sockaddr*)&info->local_addr) << endl;
					        cout << "------------------------------------------------------------------------\n";
					        memcpy(data+elt, &info->remote_addr, sizeof(info->remote_addr));					
				        };
			        } else {
				        return [](struct connection_info *, size_t) { /* only called with zero payload */ };
			        }
			
		        });


	get_all_cxn<std::function<std::function<void(struct connection_info *, size_t)>(size_t) >,  
	            std::function<void(struct connection_info *, size_t)> >(sock, callgen);

	if (data) {
		munmap(data, data_len);
	}

	close(sock);
}
