from struct import *
import socket
from datetime import *

from common import *
import traceback;

class log_types(object):
    DEBUG = 0x2; #/* interpret as a string */
    CTRL = 0x4; #/* control messages */
    ERROR = 0x8; #/* strings */
    BITCOIN = 0x10; #/* general status information (strings) */
    BITCOIN_MSG = 0x20; #/* actual incoming/outgoing messages as encoded */
    CONNECTOR = 0x40;
    CLIENT = 0x80;

    str_mapping = {
        0x2 : 'DEBUG',
        0x4 : 'CTRL',
        0x8 : 'ERROR',
        0x10 : 'BITCOIN',
        0x20 : 'BITCOIN_MSG',
        0x40 : 'CONNECTOR',
        0x80 : 'CLIENT',
    }

class update_types(object):
    CONNECT_SUCCESS = 0x1;# // We initiated the connection 
    ACCEPT_SUCCESS = 0x2;# // They initiated the connection (i.e., the result of an accept)
    ORDERLY_DISCONNECT = 0x4;# // Attempt to read returns 0
    WRITE_DISCONNECT = 0x8;# // Attempt to write returns error, disconnected
    UNEXPECTED_ERROR = 0x10;# // We got some kind of other error, indicating potentially iffy state, so disconnected
    CONNECT_FAILURE = 0x20;# // We initiated a connection, but if failed.
    PEER_RESET = 0x40;# // connection reset by peer
    CONNECTOR_DISCONNECT = 0x80;# // we initiated a disconnect

    str_mapping = {
        0x1 : 'CONNECT_SUCCESS',
        0x2 : 'ACCEPT_SUCCESS',
        0x4 : 'ORDERLY_DISCONNECT',
        0x8 : 'WRITE_DISCONNECT',
        0x10 : 'UNEXPECTED_ERROR',
        0x20 : 'CONNECT_FAILURE',
        0x40 : 'PEER_RESET',
        0x80 : 'CONNECTOR_DISCONNECT',
    }

class log(object):
    def __init__(self, log_type, source_id, timestamp, rest):
        self.source_id = source_id
        self.log_type = log_type;
        self.timestamp = timestamp
        self.rest = rest

    @staticmethod
    def deserialize_parts(serialization):
        source_id, log_type, timestamp = unpack('>IBQ', serialization[:13])
        rest = serialization[13:]
        return (source_id, log_type, timestamp, rest);

    @staticmethod
    def deserialize(log_type, source_id, timestamp, rest):
        return log(log_type, source_id, timestamp, rest)

    def __str__(self):
        return "[{0}] ({1}) {2}: {3}".format(unix2str(self.timestamp), self.source_id, log_types.str_mapping[self.log_type], self.rest);

class debug_log(log):
    @staticmethod
    def deserialize(source_id, timestamp, rest):
        return debug_log(log_types.DEBUG, source_id, timestamp, rest)

class client_log(log):
    @staticmethod
    def deserialize(source_id, timestamp, rest):
        return client_log(log_types.CLIENT, source_id, timestamp, rest)


class connector_log(log):
    @staticmethod
    def deserialize(source_id, timestamp, rest):
        return connector_log(log_types.CONNECTOR, source_id, timestamp, rest)

class ctrl_log(log):
    @staticmethod
    def deserialize(source_id, timestamp, rest):
        return ctrl_log(log_types.CTRL, source_id, timestamp, rest)


class error_log(log):
    @staticmethod
    def deserialize(source_id, timestamp, rest):
        return error_log(log_types.ERROR, source_id, timestamp, rest)

class bitcoin_log(log): # bitcoin connection/disconnection events
    def repack(self):
        first = pack('>II', self.handle_id, self.update_type)
        second = pack('=hHI8xhHI8x', socket.AF_INET, self.r_port_, self.r_addr_, socket.AF_INET, self.l_port_, self.l_addr_)
        third = pack('>Is', len(self.text), self.text)
        return first + second + third

    def __init__(self, source_id, timestamp, handle_id, update_type, remote_addr, remote_port, local_addr, local_port, text):
        self.handle_id = handle_id
        self.r_addr_ = inet_aton(remote_addr)
        self.r_port_ = socket.htons(remote_port);
        self.l_addr_ = inet_aton(local_addr)
        self.l_port_ = socket.htons(local_port);
        self.update_type = update_type 
        self.text = text
        rest = self.repack()
        super(bitcoin_log, self).__init__(log_types.BITCOIN, source_id, timestamp, rest)

    def __str__(self):
        return "[{0}] ({1}) {2}: handle: {3} update_type: {4}, remote: {5}:{6}, local: {7}:{8}, text: {9}".format(unix2str(self.timestamp), self.source_id, log_types.str_mapping[self.log_type], self.handle_id, update_types.str_mapping[self.update_type], self.remote_addr, self.remote_port, self.local_addr, self.local_port, self.text);

    @staticmethod
    def deserialize(source_id, timestamp, rest):
        handle_id, update_type = unpack('>II', rest[:8])
        fam1, r_port_, r_addr_, fam2, l_port_, l_addr_ = unpack('=hHI8xhHI8x', rest[8:40])
        text_len, = unpack('>I', rest[40:44])
        if text_len > 0:
            text, = unpack('>{0}s'.format(text_len), rest[44:])
        else:
            text = ''
        return bitcoin_log(source_id, timestamp, handle_id, update_type, 
                           inet_ntoa(r_addr_), socket.ntohs(r_port_), 
                           inet_ntoa(l_addr_), socket.ntohs(l_port_), text)

    @property
    def remote_addr(self):
        return inet_ntoa(self.r_addr_)

    @property
    def remote_port(self):
        return socket.ntohs(self.r_port_)

    @property
    def local_addr(self):
        return inet_ntoa(self.l_addr_)

    @property
    def local_port(self):
        return socket.ntohs(self.l_port_)

class bitcoin_msg_log(log):
    def repack(self):
        return pack('>I?', self.handle_id, self.is_sender) + self.bitcoin_msg

    def __init__(self, source_id, timestamp, handle_id, is_sender, bitcoin_msg):
        self.handle_id = handle_id;
        self.is_sender = is_sender
        self.bitcoin_msg = bitcoin_msg
        rest = self.repack()
        super(bitcoin_msg_log, self).__init__(log_types.BITCOIN_MSG, source_id, timestamp, rest)

    @staticmethod
    def deserialize(source_id, timestamp, rest):
        handle_id, is_sender = unpack('>I?', rest[:5])
        return bitcoin_msg_log(source_id, timestamp, handle_id, is_sender, rest[5:])

    def __str__(self):
        return "[{0}] ({1}) {2}: handle_id: {3}, is_sender: {4}, bitcoin_msg: (ommitted)".format(unix2str(self.timestamp), self.source_id, log_types.str_mapping[self.log_type], self.handle_id, self.is_sender)

type_to_obj = {
    log_types.DEBUG : debug_log,
	log_types.CTRL : ctrl_log,
	log_types.ERROR : error_log,
	log_types.BITCOIN : bitcoin_log,
	log_types.BITCOIN_MSG : bitcoin_msg_log,
	log_types.CONNECTOR : connector_log,
	log_types.CLIENT : client_log
}
