#include <EEPROM.h>

#define NCHAN 7   // max 8
#define ESLEN 40  // includes null-terminator

#define LAN_OFF    0
#define LAN_DHCP   1
#define LAN_STATIC 2

// EEPROM addresses:
#define EPA_COMMIT            0
#define EPA_SCPI              4
#define EPA_SCPI_LAN          80
#define EPA_IDN               100
#define EPA_REPLY_READONLY    140
#define EPA_REPLY_INVALID_CMD 180
#define EPA_REPLY_INVALID_ARG 220
#define EPA_REPLY_REBOOT_REQ  260
#define EPA_REPLY_REBOOTING   300

struct SCPI
{
    byte     input_mode   [NCHAN];  // :INput<n>:MODe           OFF, RISing, FALLing, CHAnge
    bool     input_invert [NCHAN];  // :INput<n>:INVert         0 = non-inverting, 1 = inverting
    bool     output_serial;         // :OUTput:SERial:ENable    0 = disabled, 1 = enabled
    bool     output_udp;            // :OUTput:UDP:ENable       0 = disabled, 1 = enabled
    uint32_t output_udp_dest;       // :OUTput:UDP:DESTination  ip address
    uint16_t output_udp_port;       // :OUTput:UDP:PORT         port number
};

struct SCPI_LAN
{
    byte mode;                // :SYSTem:COMMunicate:LAN:MODe            OFF, DHCP, or STATic
    byte mac[6];              // :SYSTem:COMMunicate:LAN:MAC             MAC address (eg. 1A:2B:3C:4D:5E:6F)
    uint32_t ip_static;       // :SYSTem:COMMunicate:LAN:IP:STATic       static ip address (eg. 192.168.0.100)
    uint32_t gateway_static;  // :SYSTem:COMMunicate:LAN:GATEway:STATic  static gateway address
    uint32_t subnet_static;   // :SYSTem:COMMunicate:LAN:SUBnet:STATic   static subnet mask
};

// notes on SCPI settings:
//   - bool values must be 0 or 1
//   - <n> in input configs is {1, 2, 3, 4, 5, 6, 7} for outputs {A, B, C, D, E, F, G}
//   - abbreviations are supported where noted, e.g INVert matches both INV and INVERT
//   - LAN settings do not take effect until reboot!

void scpi_default(SCPI &s)
{
    for (int n = 0; n < NCHAN; n++)
    {
        s.input_mode[n]   = (n == 0 ? RISING : 0);  // 0 means OFF, other options are non-zero
        s.input_invert[n] = 0;
    }

    s.output_serial   = 1;
    s.output_udp      = 0;
    s.output_udp_dest = 0xC800A8C0;  // 192.168.0.200
    s.output_udp_port = 5000;
}
