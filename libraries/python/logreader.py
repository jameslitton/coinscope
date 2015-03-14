#!/usr/bin/env python

import sys
from struct import *

sys.path.append('lib')

import logger;

# This just reads a log file (verbatim.out) from the current working
# directory and prints it out. It's meant to both test and demonstrate
# use of the logger.py classes

# Set the log type you are interested in here 
interests = ~(logger.log_types.BITCOIN_MSG | logger.log_types.BITCOIN)

for filename in sys.argv[1:]:
    fp = open(filename, 'rb')

    while(True):
        length = fp.read(4)
        if length is None or len(length) < 4:
            break
        length, = unpack('>I', length)
        record = fp.read(length)
        source_id, log_type, timestamp, rest = logger.log.deserialize_parts(record)
        log = logger.type_to_obj[log_type].deserialize(source_id, timestamp, rest)

        if (log_type & interests) :# and log.rest == "Initiating GETADDR probe":
            print log

    fp.close()

