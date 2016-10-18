import serial
import socket
import datetime

basevars_full   = ['*IDN',':OUTPUT:SERIAL:ENABLE', ':OUTPUT:UDP:ENABLE', ':OUTPUT:UDP:DESTINATION', ':OUTPUT:UDP:PORT']
basevars_short  = ['*IDN',':OUT:SER:EN',           ':OUT:UDP:EN',        ':OUT:UDP:DEST',           ':OUT:UDP:PORT']
inputvars_full  = [':MODE', ':PULLUP', ':INVERT', ':COUNT', ':VALUE']
inputvars_short = [':MOD',  ':PULL',   ':INV',    ':COUN',  ':VAL']
lanvars_full    = [':SYSTEM:COMMUNICATE:LAN:MODE', ':SYSTEM:COMMUNICATE:LAN:MAC', ':SYSTEM:COMMUNICATE:LAN:IP', ':SYSTEM:COMMUNICATE:LAN:GATEWAY', ':SYSTEM:COMMUNICATE:LAN:SUBNET', ':SYSTEM:COMMUNICATE:LAN:IP:STATIC', ':SYSTEM:COMMUNICATE:LAN:GATEWAY:STATIC', ':SYSTEM:COMMUNICATE:LAN:SUBNET:STATIC']
lanvars_short   = [':SYST:COMM:LAN:MOD',           ':SYST:COMM:LAN:MAC',          ':SYST:COMM:LAN:IP',          ':SYST:COMM:LAN:GATE',             ':SYST:COMM:LAN:SUB',             ':SYST:COMM:LAN:IP:STAT',            ':SYST:COMM:LAN:GATE:STAT',               ':SYST:COMM:LAN:SUB:STAT']

def lj_len(short) : return 26 if short else 40

def decode_edges(y_old, y_new) :
    rv = {}
    for n in range(0, 8) :
        x_old = (y_old >> n) & 0x1
        x_new = (y_new >> n) & 0x1
        if x_old != x_new : rv[chr(ord('A') + n)] = 'RISING' if x_new else 'FALLING'
    return rv

class Detectron :

    def trig(self)   : return self.query('*TRG')
    def save(self)   : return self.query('*SAV')
    def recall(self) : return self.query('*RCL')
    def reset(self)  : return self.query('*RST')
    def reboot(self) : return self.query(':SYSTEM:REBOOT')

    def dump_base(self, short=False) :
        for var in (basevars_short if short else basevars_full) :
            msg = var + '?'
            print(msg.ljust(lj_len(short)) + self.query(msg).strip())

    def dump_input(self, chan=1, short=False) :
        for var in (inputvars_short if short else inputvars_full) :
            msg = (':IN' if short else ':INPUT') + str(chan) + var + '?'
            print(msg.ljust(lj_len(short)) + self.query(msg).strip())

    def dump_lan(self, short=False) :
        for var in (lanvars_short if short else lanvars_full) :
            msg = var + '?'
            print(msg.ljust(lj_len(short)) + self.query(msg).strip())

    def dump_all(self, short=False) :
        self.dump_base(short=short)
        for chan in range(1, 8) :
            self.dump_input(chan=chan, short=short)
        self.dump_lan(short=short)

class DetectronSerial(Detectron, serial.Serial) :

    def __init__(self, port, timeout=1) :
        serial.Serial.__init__(self, port=port, timeout=timeout)

    def query(self, msg) :
        self.write(msg + '\n')
        return self.readline()

    def listen(self) :
        timeout = self.timeout
        self.timeout = None
        try :
            while True :
                data = self.readline()
                dt = datetime.datetime.now()
                y_old = ord(data[0]);
                y_new = ord(data[1]);
                print('%s 0x%x -> 0x%x %s' % (dt, y_old, y_new, decode_edges(y_old, y_new)))
        except (KeyboardInterrupt, SystemExit) : pass
        finally : self.timeout = timeout

class DetectronTCP(Detectron, socket.socket) :

    def __init__(self, ip_addr, port=18, timeout=1) :
        socket.socket.__init__(self, socket.AF_INET, socket.SOCK_STREAM)
        self.connect((ip_addr, port))
        self.settimeout(timeout)

    def query(self, msg) :
        self.sendall(msg + '\n')
        reply = ''
        while True :
            reply += self.recv(512)
            if reply.endswith('\r') or reply.endswith('\n') : break
        return reply

class DetectronUDP(socket.socket) :

    def __init__(self, if_addr='', port=5000) :
        socket.socket.__init__(self, socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        self.bind((if_addr, port))

    def listen(self) :
        try :
            while True :
                (data, (addr, port)) = self.recvfrom(2)
                dt = datetime.datetime.now()
                y_old = ord(data[0]);
                y_new = ord(data[1]);
                print('%s %s 0x%x -> 0x%x %s' % (dt, addr, y_old, y_new, decode_edges(y_old, y_new)))
        except (KeyboardInterrupt, SystemExit) : pass
