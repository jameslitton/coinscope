from struct import pack,unpack
import socket

from common import *

class commands(object):
    COMMAND_GET_CXN = 1;
    COMMAND_DISCONNECT = 2;
    COMMAND_SEND_MSG = 3;

    str_mapping = {
        1 : 'COMMAND_GET_CXN',
        2 : 'COMMAND_DISCONNECT',
        3 : 'COMMAND_SEND_MSG'
    }

class message_types(object):
    BITCOIN_PACKED_MESSAGE = 1;
    COMMAND = 2;
    REGISTER = 3;
    CONNECT = 4;

    str_mapping = {
        1 : 'BITCOIN_PACKED_MESSAGE',
        2 : 'COMMAND',
        3 : 'REGISTER',
        4 : 'CONNECT',
    }

class targets(object):
    BROADCAST = 0xFFFFFFFF;


class message(object): # Just a generic message
    def __init__(self, message_type, payload):
        self.version = 0; # not in the API because changes there should require monkeying with this
        self.message_type = message_type
        self.payload = payload

    def serialize(self):
        return pack('>BIB', self.version, len(self.payload), self.message_type) + self.payload

    @staticmethod
    def deserialize(serialization):
        version, length, message_type = unpack('>BIB', serialization[:6]);
        payload = serialization[6:]
        if version != 0 or length != len(payload):
            raise Exception(res);
        return message(message_type, payload)

class register_msg(message): # Just re-registers the client connection. Use this to garbage collect all registered messages
    def __init__(self):
        super(register_message, self).__init__(message_types.REGISTER, '');

    @staticmethod
    def deserialize(serialization):
        version, length, message_type = unpack('>BIB', serialization[:6]);
        payload = serialization[6:]
        if version != 0 or length != len(payload) or message_type != message_types.REGISTER:
            raise Exception(res);
        return register_msg()

class bitcoin_msg(message):
    # payload is a protocol encoded bitcoin message
    def __init__(self, payload):
        super(bitcoin_msg,self).__init__(message_types.BITCOIN_PACKED_MESSAGE, payload)

    @staticmethod
    def deserialize(serialization):
        version, length, message_type = unpack('>BIB', serialization[:6]);
        payload = serialization[6:]
        if version != 0 or length != len(payload) or message_type != message_types.BITCOIN_PACKED_MESSAGE:
            raise Exception(res);
        return bitcoin_msg(payload)

        
    @property
    def bitcoin_msg(self):
        return self.payload

    @bitcoin_msg.setter
    def bitcoin_msg(self,value):
        self.payload = value;

class connect_msg(message):

    def repack(self):
        return pack('=hHI8xhHI8x', socket.AF_INET, self.r_port_, self.r_addr_, socket.AF_INET, self.r_port_, self.r_addr_)

    def __init__(self, remote_addr, remote_port, local_addr, local_port):
        self.r_addr_ = inet_aton(remote_addr)
        self.r_port_ = socket.htons(remote_port);
        self.l_addr_ = inet_aton(local_addr)
        self.l_port_ = socket.htons(local_port);

        super(connect_msg, self).__init__(message_types.CONNECT, self.repack())

    @staticmethod
    def deserialize(serialization):
        version, length, message_type = unpack('>BIB', serialization[:6]);
        payload = serialization[6:]
        fam1, r_port_, r_addr_, fam2, l_port_, l_addr_ = unpack('=hHI8xhHI8x', payload)
        if version != 0 or length != len(payload) or message_type != message_types.CONNECT:
            raise Exception(res);
        if fam1 != socket.AF_INET or fam2 != socket.AF_INET:
            raise Exception("bad family")
        return connect_msg(inet_ntoa(r_addr_), socket.ntohs(r_port_), inet_ntoa(l_addr_), socket.ntohs(l_port_))

    @property 
    def remote_addr(self):
        return inet_ntoa(self.r_addr_)

    @remote_addr.setter
    def remote_addr(self,value):
        self.r_addr_ = inet_aton(remote_addr)
        self.payload = self.repack();

    @property 
    def remote_port(self):
        return socket.ntohs(self.r_port_)

    @remote_port.setter
    def remote_port(self,value):
        self.r_port_ = socket.htons(value)
        self.payload = self.repack()
        

    @property 
    def local_addr(self):
        return inet_ntoa(self.l_addr_)

    @local_addr.setter
    def local_addr(self,value):
        self.l_addr_ = inet_aton(local_addr)
        self.payload = self.repack();

    @property 
    def local_port(self):
        return socket.ntohs(self.l_port_)

    @local_port.setter
    def local_port(self,value):
        self.l_port_ = socket.htons(value)
        self.payload = self.repack()
        


class command_msg(message):

    def repack(self):
        if (len(self.targets_) > 0):
            packstr = '>BII{0}I'.format(len(self.targets_));
            tl = map(socket.htonl, self.targets_)
            return pack(packstr, self.command_, self.message_id_, len(self.targets_), *tl);
        else:
            return pack('>BII', self.command_, self.message_id_, 0)

    def __init__(self, command, message_id, targets_list=()):
        # Generate payload and then just call parent constructor
        self.command_ = command
        self.message_id_ = message_id
        self.targets_ = targets_list
        super(command_msg, self).__init__(message_types.COMMAND, self.repack())

    @staticmethod
    def deserialize(serialization):
        version, length, message_type = unpack('>BIB', serialization[:6]);
        payload = serialization[6:]

        if version != 0 or length != len(payload) or message_type != message_types.COMMAND:
            raise Exception(res);


        # god python's pack is awful
        if (len(payload) < 9 or (len(payload) - 9) % 4 != 0):
            raise Exception("bad payload", len(payload))
        ints = (len(payload) - 9) / 4
        targets = ()

        if (ints > 0):
            packstr = '>BII{0}I'.format(ints);
        else:
            packstr = '>BII'

        res = unpack(packstr, payload);

        command = res[0]
        message_id = res[1]
        targets = res[3:]

        if res[2] != len(targets):
            raise Exception("target count and target list mismatch")
        return command_msg(command, message_id, targets)


    @property
    def command(self):
        return self.command_

    @command.setter
    def command(self, value):
        self.command_ = value;
        self.payload = self.repack();

    @property
    def message_id(self):
        return self.message_id_

    @message_id.setter
    def message_id(self,value):
        self.message_id_ = value;
        self.payload = self.repack();

    @property
    def targets(self):
        return self.targets_

    @targets.setter
    def targets(self,value):
        self.targets_ = value;
        self.payload = self.repack();

def deserialize_message(serialization):
    version, length, message_type = unpack('>BIB', serialization[:6]);
    payload = serialization[6:]
    if version != 0 or length != len(payload):
        raise Exception(res);

    if not type_to_obj.has_key(message_type):
        raise Exception(res)

    # Yes, this does cause the unpack to happen again. I'm not very
    # good with python with respect to getting rid of that kind of
    # thing. If you find youself doing a lot of blind deserialization
    # (I figure this is mostly for debugging) you'll want to fix that)
    return type_to_obj[message_type].deserialize(serialization);

class connection_info(object):
    # largely a copy of another structure above. Should be refactored
    def repack(self):
        return pack('>I', self.handle_id) + pack('=hHI8xhHI8x', socket.AF_INET, self.r_port_, self.r_addr_, socket.AF_INET, self.r_port_, self.r_addr_)

    def __init__(self, handle_id, remote_addr, remote_port, local_addr, local_port):
        self.handle_id = handle_id
        self.r_addr_ = inet_aton(remote_addr)
        self.r_port_ = socket.htons(remote_port);
        self.l_addr_ = inet_aton(local_addr)
        self.l_port_ = socket.htons(local_port);

    @staticmethod
    def deserialize(serialization):
        handle_id = unpack('>I', serialization[:4])
        payload = serialization[4:]
        fam1, r_port_, r_addr_, fam2, l_port_, l_addr_ = unpack('=hHI8xhHI8x', payload)
        if fam1 != socket.AF_INET or fam2 != socket.AF_INET:
            raise Exception("bad family")
        return connection_info(handle_id, 
                               inet_ntoa(r_addr_), 
                               socket.ntohs(r_port_), 
                               inet_ntoa(l_addr_), 
                               socket.ntohs(l_port_))

    @property 
    def remote_addr(self):
        return inet_ntoa(self.r_addr_)

    @remote_addr.setter
    def remote_addr(self,value):
        self.r_addr_ = inet_aton(remote_addr)
        self.payload = self.repack();

    @property 
    def remote_port(self):
        return socket.ntohs(self.r_port_)

    @remote_port.setter
    def remote_port(self,value):
        self.r_port_ = socket.htons(value)
        self.payload = self.repack()
        

    @property 
    def local_addr(self):
        return inet_ntoa(self.l_addr_)

    @local_addr.setter
    def local_addr(self,value):
        self.l_addr_ = inet_aton(local_addr)
        self.payload = self.repack();

    @property 
    def local_port(self):
        return socket.ntohs(self.l_port_)

    @local_port.setter
    def local_port(self,value):
        self.l_port_ = socket.htons(value)
        self.payload = self.repack()
        


type_to_obj = {
    message_types.BITCOIN_PACKED_MESSAGE : bitcoin_msg,
    message_types.COMMAND : command_msg,
    message_types.REGISTER : register_msg,
    message_types.CONNECT : connect_msg
}


