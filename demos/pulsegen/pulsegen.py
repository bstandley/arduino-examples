import serial
import socket

basevars_full   = ['*IDN',':TRIGGER:EDGE', ':TRIGGER:ARMED', ':TRIGGER:READY', ':TRIGGER:REARM', ':TRIGGER:COUNT', ':CLOCK:SRC', ':CLOCK:EDGE', ':CLOCK:FREQUENCY', ':CLOCK:FREQUENCY:MEASURE', ':CLOCK:FREQUENCY:INTERNAL', ':CLOCK:FREQUENCY:EXTERNAL']
basevars_short  = ['*IDN',':TRIG:EDGE',    ':TRIG:ARM',      ':TRIG:READY',    ':TRIG:REARM',    ':TRIG:COUN',     ':CLOCK:SRC', ':CLOCK:EDGE', ':CLOCK:FREQ',      ':CLOCK:FREQ:MEAS',         ':CLOCK:FREQ:INT',           ':CLOCK:FREQ:EXT']
pulsevars_full  = [':DELAY', ':WIDTH', ':PERIOD', ':CYCLES', ':VALID', ':INVERT']
pulsevars_short = [':DEL',   ':WID',   ':PER',    ':CYC',    ':VAL',   ':INV']
lanvars_full    = [':SYSTEM:COMMUNICATE:LAN:MODE', ':SYSTEM:COMMUNICATE:LAN:MAC', ':SYSTEM:COMMUNICATE:LAN:IP', ':SYSTEM:COMMUNICATE:LAN:IP:STATIC', ':SYSTEM:COMMUNICATE:LAN:GATEWAY:STATIC', ':SYSTEM:COMMUNICATE:LAN:SUBNET:STATIC']
lanvars_short   = [':SYST:COMM:LAN:MODE',          ':SYST:COMM:LAN:MAC',          ':SYST:COMM:LAN:IP',          ':SYST:COMM:LAN:IP:STAT',            ':SYST:COMM:LAN:GATE:STAT',               ':SYST:COMM:LAN:SUB:STAT']

def lj_len(short) : return 26 if short else 40

class Pulsegen :

    def trig(self)   : return self.query('*TRG')
    def save(self)   : return self.query('*SAV')
    def recall(self) : return self.query('*RCL')
    def reset(self)  : return self.query('*RST')
    def reboot(self) : return self.query(':SYSTEM:REBOOT')

    def dump_base(self, short=False) :
        for var in (basevars_short if short else basevars_full) :
            msg = var + '?'
            print(msg.ljust(lj_len(short)) + self.query(msg).strip())

    def dump_pulse(self, chan=1, short=False) :
        for var in (pulsevars_short if short else pulsevars_full) :
            msg = (':PULS' if short else ':PULSE') + str(chan) + var + '?'
            print(msg.ljust(lj_len(short)) + self.query(msg).strip())

    def dump_lan(self, short=False) :
        for var in (lanvars_short if short else lanvars_full) :
            msg = var + '?'
            print(msg.ljust(lj_len(short)) + self.query(msg).strip())

    def dump_all(self, short=False) :
        self.dump_base(short=short)
        for chan in range(1, 5) :
            self.dump_pulse(chan=chan, short=short)
        self.dump_lan(short=short)

    def set_pulse(self, chan=1, delay=0.04, width=0.005, period=0.02, cycles=3, invert=0) :
        msg = ':PULS%d:DEL %f' % (chan, delay)
        print(msg.ljust(20) + self.query(msg).strip())
        msg = ':PULS%d:WID %f' % (chan, width)
        print(msg.ljust(20) + self.query(msg).strip())
        msg = ':PULS%d:PER %f' % (chan, period)
        print(msg.ljust(20) + self.query(msg).strip())
        msg = ':PULS%d:CYC %d' % (chan, cycles)
        print(msg.ljust(20) + self.query(msg).strip())
        msg = ':PULS%d:INV %d' % (chan, invert)
        print(msg.ljust(20) + self.query(msg).strip())

class PulsegenSerial(Pulsegen, serial.Serial) :

    def __init__(self, port, timeout=1) :
        serial.Serial.__init__(self, port=port, timeout=timeout)

    def query(self, msg) :
        self.write(msg + '\n')
        return self.readline()

class PulsegenSocket(Pulsegen, socket.socket) :

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
