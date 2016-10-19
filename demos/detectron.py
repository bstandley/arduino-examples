import sdi
import datetime
import socket

class Detectron :
    NCHAN = 7

    def trig(self) :
        return self.query('*TRG')

    def dump_output(self) :
        self.dump([':OUTput:' + s for s in ['SERial:ENable', 'UDP:ENable', 'UDP:DESTination', 'UDP:PORT']])

    def dump_input(self, chan=1) :
        self.dump([':INput%d:' % chan + s for s in ['MODe', 'PULLup', 'INVert', 'COUNt', 'VALue']])

    def dump_all(self, short=False) :
        print('device info:')
        self.dump(['*IDN'])
        print('output config:')
        self.dump_output()
        print('input config:')
        for chan in range(1, self.NCHAN + 1) :
            self.dump_input(chan=chan)
        print('network config:')
        self.dump_lan()

class DetectronSerial(Detectron, sdi.SDISerial) :

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

class DetectronSocket(Detectron, sdi.SDISocket) : pass

def decode_edges(y_old, y_new) :
    rv = {}
    for n in range(0, 8) :
        x_old = (y_old >> n) & 0x1
        x_new = (y_new >> n) & 0x1
        if x_old != x_new : rv[chr(ord('A') + n)] = 'RISING' if x_new else 'FALLING'
    return rv

def listen_udp(if_addr='', port=5000) :
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.bind((if_addr, port))
    try :
        while True :
            (data, (addr, port)) = sock.recvfrom(2)
            dt = datetime.datetime.now()
            y_old = ord(data[0]);
            y_new = ord(data[1]);
            print('%s %s 0x%x -> 0x%x %s' % (dt, addr, y_old, y_new, decode_edges(y_old, y_new)))
    except (KeyboardInterrupt, SystemExit) : pass
    finally : sock.close()
