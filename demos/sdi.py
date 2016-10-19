import re
import serial
import socket

class SDI :
    def __init__(self, shorten) :
        self.shorten = shorten

    def save(self) :
        return self.query('*SAV')

    def recall(self) :
        return self.query('*RCL')

    def reset(self) :
        return self.query('*RST')

    def reboot(self) :
        return self.query(':SYSTEM:REBOOT')

    def dump(self, msgs) :
        my_msgs = [(re.sub('[a-z]', '', msg) if self.shorten else msg.upper()) + '?' for msg in msgs]
        max_len = max([len(msg) for msg in my_msgs])
        for msg in my_msgs :
            print('  ' + msg.ljust(max_len) + ' ' + self.query(msg).strip())

    def dump_lan(self) :
        self.dump([':SYSTem:COMMunicate:LAN:' + s for s in ['MODe', 'MAC', 'IP', 'GATEway', 'SUBnet', 'IP:STATic', 'GATEway:STATic', 'SUBnet:STATic']])

class SDISerial(SDI, serial.Serial) :
    def __init__(self, port, timeout=1, shorten=False) :
        serial.Serial.__init__(self, port=port, timeout=timeout)
        SDI.__init__(self, shorten)

    def query(self, msg) :
        self.write(msg + '\n')
        return self.readline()

class SDISocket(SDI, socket.socket) :
    def __init__(self, ip_addr, port=18, timeout=1, shorten=False) :
        socket.socket.__init__(self, socket.AF_INET, socket.SOCK_STREAM)
        self.connect((ip_addr, port))
        self.settimeout(timeout)
        SDI.__init__(self, shorten)

    def query(self, msg) :
        self.sendall(msg + '\n')
        reply = ''
        while True :
            reply += self.recv(512)
            if reply.endswith('\r') or reply.endswith('\n') : break
        return reply
