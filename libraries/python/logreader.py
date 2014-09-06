import sys
from struct import *

sys.path.append('lib')

import logger;

# This just reads a log file (verbatim.out) from the current working
# directory and prints it out. It's meant to both test and demonstrate
# use of the logger.py classes

fp = open('verbatim.out', 'rb') # This could be a socket to logserver


while(True):
    length = fp.read(4)
    if length is None or length == 0:
        break
    length, = unpack('>I', length)
    record = fp.read(length)
    source_id, log_type, timestamp, rest = logger.log.deserialize_parts(record)
    log = logger.type_to_obj[log_type].deserialize(source_id, timestamp, rest)
    print log
    
