#include <EEPROM.h>

#define NCHAN 7
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
#define EPA_REPLY_NA          340

struct SCPI
{
    byte dio_dir    [NCHAN];  // :DIO<n>:DIRection     INput, OUTput
    bool dio_invert [NCHAN];  // :DIO<n>:INVert        0 = non-inverting, 1 = inverting
    bool dio_pullup [NCHAN];  // :DIO<n>:INput:PULLup  0 = floating input, 1 = enable pull-up resistor
    bool dio_setval [NCHAN];  // :DIO<n>:OUTput:VALue  0 = low, 1 = high (with inversion applied)
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
//   - <n> in channel configs is {1, 2, 3, 4, 5, 6, 7} for outputs {A, B, C, D, E, F, G}
//   - abbreviations are supported where noted, e.g INVert matches both INV and INVERT
//   - LAN settings do not take effect until reboot!

void scpi_default(SCPI &s)
{
    for (int n = 0; n < NCHAN; n++)
    {
        s.dio_dir[n]    = INPUT;
        s.dio_invert[n] = 0;
        s.dio_pullup[n] = 1;
        s.dio_setval[n] = 0;
    }
}
