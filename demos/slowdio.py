import sdi

class SlowDIO :
    NCHAN = 7

    def dump_dio(self, chan=1) :
        self.dump([':DIO%d:' % chan + s for s in ['DIRection', 'INVert', 'INput:PULLup', 'INput:VALue', 'OUTput:VALue', 'VALue']])

    def dump_all(self, short=False) :
        print('device info:')
        self.dump(['*IDN'])
        print('dio config:')
        for chan in range(1, self.NCHAN + 1) :
            self.dump_dio(chan=chan)
        print('network config:')
        self.dump_lan()

class SlowDIOSerial(SlowDIO, sdi.SDISerial) : pass
class SlowDIOSocket(SlowDIO, sdi.SDISocket) : pass
