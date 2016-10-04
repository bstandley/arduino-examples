#include <EEPROM.h>

#define NCHAN 4
#define ESLEN 40  // includes null-terminator

#define LAN_OFF    0
#define LAN_DHCP   1
#define LAN_STATIC 2

// EEPROM addresses:
#define EPA_COMMIT            0
#define EPA_SCPI              4
#define EPA_SCPI_LAN          80
#define EPA_IDN               100
#define EPA_REPLY_CHECK       140
#define EPA_REPLY_READONLY    180
#define EPA_REPLY_INVALID_CMD 220
#define EPA_REPLY_INVALID_ARG 260
#define EPA_REPLY_REBOOT_REQ  320
#define EPA_REPLY_REBOOTING   360

struct SCPI
{
    byte clock_src;             // :CLOCK:SRC            INTernal or EXTernal
    long clock_freq_ext;        // :CLOCK:FREQ:EXTernal  ideal external frequency in Hz -- max 5e6
    byte trig_edge;             // :TRIG:EDGE            RISing or FALLing
    bool trig_rearm;            // :TRIG:REARM           rearm after pulse sequence and on reboot
    long pulse_delay  [NCHAN];  // :PULSe<n>:DELay       delay to first pulse is s (stored in us)
    long pulse_width  [NCHAN];  // :PULSe<n>:WIDth       pulse width in s (stored in us)
    long pulse_period [NCHAN];  // :PULSe<n>:PERiod      pulse period in s (stored in us)
    long pulse_cycles [NCHAN];  // :PULSe<n>:CYCles      number of pulses
    bool pulse_invert [NCHAN];  // :PULSe<n>:INVert      0 = non-inverting, 1 = inverting
};

struct SCPI_LAN
{
    byte mode;                // :SYSTem:COMMunicate:LAN:MODE            OFF, DHCP, or STATic
    byte mac            [6];  // :SYSTem:COMMunicate:LAN:MAC             MAC address (eg. 1A:2B:3C:4D:5E:6F)
    byte ip_static      [4];  // :SYSTem:COMMunicate:LAN:IP:STATic       static ip address (eg. 192.168.0.100)
    byte gateway_static [4];  // :SYSTem:COMMunicate:LAN:GATEway:STATic  static gateway address
    byte subnet_static  [4];  // :SYSTem:COMMunicate:LAN:SUBnet:STATic   static subnet mask
};

// notes on SCPI settings:
//   - bool values must be 0 or 1
//   - <n> in pulse configs is {1, 2, 3, 4} for outputs {A, B, C, D}
//   - abbreviations are supported where noted, e.g WIDth matches both WID and WIDTH
//   - if WIDTH > PERIOD, the pulse is continuous, i.e. always high if not inverted, full sequence will last DELAY + CYCLES*PERIOD
//   - (DELAY + PERIOD*CYCLES)/FREQ must be < 4e9, otherwise the channel will not be used (VALID = 0)
//   - prefix :SYSTem:COMMunicate is optional
//   - LAN settings do not take effect until reboot!

void scpi_default(SCPI &s)
{
    s.clock_src      = INTERNAL;
    s.clock_freq_ext = 1000000;
    s.trig_edge      = RISING;
    s.trig_rearm     = 1;

    for (int n = 0; n < NCHAN; n++)
    {
        s.pulse_delay[n]  = 0;
        s.pulse_width[n]  = 10000;
        s.pulse_period[n] = 20000;
        s.pulse_cycles[n] = (n == 0);
        s.pulse_invert[n] = 0;
    }
}
