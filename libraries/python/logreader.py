import sys
from struct import *

sys.path.append('lib')

import logger;

# This just reads a log file (verbatim.out) from the current working
# directory and prints it out. It's meant to both test and demonstrate
# use of the logger.py classes

fp = open('verbatim.out', 'rb')


while(True):
    length = fp.read(4)
    if length is None:
        break
    length, = unpack('>I', length)
    record = fp.read(length)
    log_type, timestamp, rest = logger.log.deserialize_parts(record)
    
    log = logger.type_to_obj[log_type].deserialize(timestamp, rest)
    print log
    
