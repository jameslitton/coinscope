from datetime import *

def inet_ntoa(num):
    # python version wants the num to be packed
    return str(num & 0xff) + '.' + str((num >> 8) & 0xff) + '.' + str((num >> 16) & 0xff) + '.' + str((num >> 24) & 0xff)

def inet_aton(ip):
    i = map(int, ip.split('.'))
    return (i[3] << 24) | (i[2] << 16) | (i[1] << 8) | i[0]

def unix2str(tstamp):
    dt = datetime.fromtimestamp(tstamp)
    return dt.isoformat(' ')
    
