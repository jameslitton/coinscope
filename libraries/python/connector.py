from struct import pack,unpack
import socket

class commands(object):
    COMMAND_GET_CXN = 1;
    COMMAND_DISCONNECT = 2;
    COMMAND_SEND_MSG = 3;

class message_types(object):
    BITCOIN_PACKED_MESSAGE = 1;
    COMMAND = 2;
    REGISTER= 3;
    CONNECT = 4;

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

class register_message(message): # Just re-registers the client connection. Use this to garbage collect all registered messages
    def __init__(self):
        super(register_message, self).__init__(message_types.REGISTER, '');

class bitcoin_message(message):
    # payload is a protocol encoded bitcoin message
    def __init__(self, payload):
        super(bitcoin_message,self).__init__(message_types.BITCOIN_PACKED_MESSAGE, payload)
        
    @property
    def bitcoin_msg(self):
        return self.payload

    @bitcoin_msg.setter
    def bitcoin_msg(self,value):
        self.payload = value;

class connect_msg(message):

    def repack(self):
        return pack('=hHI8xHHI8x', socket.AF_INET, self.r_port_, self.r_addr_, socket.AF_INET, self.r_port_, self.r_addr_)

    def __init__(self, remote_addr, remote_port, local_addr, local_port):
        self.r_addr_ = socket.inet_aton(remote_addr)
        self.r_port_ = socket.htons(remote_port);
        self.l_addr_ = socket.inet_aton(local_addr)
        self.l_port_ = socket.htons(local_port);

        super(command_msg, self).__init__(message_types.CONNECT, self.repack())

    @property 
    def remote_addr(self):
        return socket.inet_ntoa(self.r_addr_)

    @remote_addr.setter
    def remote_addr(self,value):
        self.r_addr_ = socket.inet_aton(remote_addr)
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
        return socket.inet_ntoa(self.l_addr_)

    @local_addr.setter
    def local_addr(self,value):
        self.l_addr_ = socket.inet_aton(local_addr)
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
            return pack(packstr, self.command_, self.message_id_, len(self.targets_), *self.targets_);
        else:
            return pack('>BII', self.command_, self.message_id_, 0)

    def __init__(self, command, message_id, targets_list=()):
        # Generate payload and then just call parent constructor
        self.command_ = command
        self.message_id_ = message_id
        self.targets_ = targets_list
        super(command_msg, self).__init__(message_types.COMMAND, self.repack())

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
