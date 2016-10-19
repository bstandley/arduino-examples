import sdi

class Pulsegen :
    NCHAN = 4

    def trig(self) :
        return self.query('*TRG')

    def set_pulse(self, chan=1, delay=0.04, width=0.005, period=0.02, cycles=3, invert=0) :
        msgs = [':PULS%d:' % chan + s for s in ['DEL %f' % delay, 'WID %f' % width, 'PER %f' % period, 'CYC %d' % cycles, 'INV %d' % invert]]
        max_len = max([len(msg) for msg in my_msgs])
        for msg in msgs :
            print(msg.ljust(max_len) + ' ' + self.query(msg).strip())

    def dump_trig(self) :
        self.dump([':TRIGger:' + s for s in ['EDGE', 'ARMed', 'READY', 'REARM', 'COUNt']])

    def dump_clock(self) :
        self.dump([':CLOCK:' + s for s in ['SRC', 'EDGE', 'FREQuency', 'FREQuency:MEASure', 'FREQuency:INTernal', 'FREQuency:EXTernal']])

    def dump_pulse(self, chan=1) :
        self.dump([':PULSe%d:' % chan + s for s in ['DELay', 'WIDth', 'PERiod', 'CYCles', 'VALid', 'INVert']])

    def dump_all(self, short=False) :
        print('device info:')
        self.dump(['*IDN'])
        print('trigger config:')
        self.dump_trig()
        print('clock config:')
        self.dump_clock()
        print('pulse config:')
        for chan in range(1, self.NCHAN + 1) :
            self.dump_pulse(chan=chan)
        print('network config:')
        self.dump_lan()

class PulsegenSerial(Pulsegen, sdi.SDISerial) : pass
class PulsegenSocket(Pulsegen, sdi.SDISocket) : pass
