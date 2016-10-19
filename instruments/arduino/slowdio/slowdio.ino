#define LAN
#define MSGLEN 64  // includes null-terminator
#define PORT   18

#include <avr/wdt.h>
#include "eeprom/shared.h"  // includes EEPROM.h
#include "comm.h"           // includes Ethernet.h, if enabled
#include "parse.h"

const unsigned long conf_commit         = 0x1234abc;  // edit to match current commit before compile/download!
const unsigned int  conf_dhcp_cycles    = 1000;
const byte          conf_dio_pin[NCHAN] = {9, 8, 7, 6, 5, 3, 2};

// SCPI commands:
//   *IDN?                model and version
//   *SAV                 save settings to EEPROM (LAN excluded)
//   *RCL                 recall EEPROM settings (also performed on startup) (LAN excluded)
//   *RST                 reset to default settings (LAN excluded)
//   :SYSTem:REBoot       reboot the device
//   :DIO<n>:VALue        current state, with inversion applied (input- or output-mode)
//   :DIO<n>:INput:VALue  current state, with inversion applied (input-mode only)

// persistent SCPI settings:
SCPI scpi;

// runtime SCPI variables (read-only except where noted):
#ifdef LAN
byte     scpi_lan_mode;     // :SYSTem:COMMunicate:LAN:MODe     actual mode (writes go to scpi_lan.mode)
#endif
uint32_t scpi_lan_ip;       // :SYSTem:COMMunicate:LAN:IP       current ip address
uint32_t scpi_lan_gateway;  // :SYSTem:COMMunicate:LAN:GATEway  current gateway address
uint32_t scpi_lan_subnet;   // :SYSTem:COMMunicate:LAN:SUBnet   current subnet mask

#ifdef LAN
EthernetServer server(PORT);
EthernetUDP udp;
unsigned int countdown_dhcp;
#endif

void setup()
{
    wdt_disable();  // just in case the bootloader does not do this automatically

    EEPROM.get(EPA_SCPI, scpi);

    for (int n = 0; n < NCHAN; n++) { update_dio(n); }

    Serial.begin(9600);

#ifdef LAN
    SCPI_LAN scpi_lan;
    EEPROM.get(EPA_SCPI_LAN, scpi_lan);

    scpi_lan_mode = scpi_lan.mode;
    if (scpi_lan_mode == LAN_DHCP)
    {
        if (Ethernet.begin(scpi_lan.mac)) { countdown_dhcp = conf_dhcp_cycles; }
        else                              { scpi_lan_mode = LAN_STATIC;        }  // fallback
    }

    if (scpi_lan_mode == LAN_STATIC)
    {
        const uint32_t dns = 0;  // DNS is not used
        Ethernet.begin(scpi_lan.mac, scpi_lan.ip_static, dns, scpi_lan.gateway_static, scpi_lan.subnet_static);
    }

    if (scpi_lan_mode != LAN_OFF) { udp.begin(0); }
#endif
    update_lan();
}

void loop()
{
    if (Serial.available() > 0)
    {
        lan = 0;  // receive/send via Serial
        char msg[MSGLEN];
        if (recv_msg(msg)) { parse_msg(msg); }
    }

#ifdef LAN
    if (scpi_lan_mode != LAN_OFF)
    {
        client = server.available();
        if (client)
        {
            lan = 1;  // receive/send via client
            char msg[MSGLEN];
            if (recv_msg(msg)) { parse_msg(msg); }
        }
    }

    if (scpi_lan_mode == LAN_DHCP)
    {
        if (countdown_dhcp == 0)
        {
            Ethernet.maintain();
            update_lan();
            countdown_dhcp = conf_dhcp_cycles;
        }
        else { countdown_dhcp--; }
    }
#endif

    delay(1);
}

bool read_input(const int n)
{
    bool x_raw = digitalRead(conf_dio_pin[n]);
    return scpi.dio_invert[n] ? !x_raw : x_raw;
}

void write_output(const int n, const bool x)
{
    bool x_raw = scpi.dio_invert[n] ? !x : x;
    digitalWrite(conf_dio_pin[n], x_raw);
}

// runtime update functions:

void update_dio(const int n)
{
    if (scpi.dio_dir[n] == OUTPUT)
    {
        pinMode(conf_dio_pin[n], OUTPUT);
        write_output(n, scpi.dio_setval[n]);
    }
    else { pinMode(conf_dio_pin[n], scpi.dio_pullup[n] ? INPUT_PULLUP : INPUT); }
}

void update_lan()
{
#ifdef LAN
    if (scpi_lan_mode != LAN_OFF)
    {
        scpi_lan_ip      = Ethernet.localIP();
        scpi_lan_gateway = Ethernet.gatewayIP();
        scpi_lan_subnet  = Ethernet.subnetMask();
        return;
    }
#endif
    scpi_lan_ip      = 0;
    scpi_lan_gateway = 0;
    scpi_lan_subnet  = 0;
}

// SCPI parsing functions:

void parse_msg(const char *msg)
{
    char rest[MSGLEN];
    bool update = 0;

    if (equal(msg, "*IDN?"))
    {
        unsigned long eeprom_commit;
        EEPROM.get(EPA_COMMIT, eeprom_commit);

        send_eps(EPA_IDN,       NOEOL);
        send_str(" (PROG: ",    NOEOL);
        send_hex(conf_commit,   NOEOL);
        send_str(", EEPROM: ",  NOEOL);
        send_hex(eeprom_commit, NOEOL);
        send_str(")");
    }
    else if (equal(msg, "*SAV"))                   { EEPROM.put(EPA_SCPI, scpi); send_str("OK"); }
    else if (equal(msg, "*RCL"))                   { EEPROM.get(EPA_SCPI, scpi); update = 1;     }
    else if (equal(msg, "*RST"))                   { scpi_default(scpi);         update = 1;     }
    else if (start(msg, ":DIO1:", rest))           { parse_dio(0, rest);                         }
    else if (start(msg, ":DIO2:", rest))           { parse_dio(1, rest);                         }
    else if (start(msg, ":DIO3:", rest))           { parse_dio(2, rest);                         }
    else if (start(msg, ":DIO4:", rest))           { parse_dio(3, rest);                         }
    else if (start(msg, ":DIO5:", rest))           { parse_dio(4, rest);                         }
    else if (start(msg, ":DIO6:", rest))           { parse_dio(5, rest);                         }
    else if (start(msg, ":DIO7:", rest))           { parse_dio(6, rest);                         }
    else if (start(msg, ":SYST", "em", ":", rest)) { parse_system(rest);                         }
    else                                           { send_eps(EPA_REPLY_INVALID_CMD);            }

    if (update)
    {
        for (int n = 0; n < NCHAN; n++) { update_dio(n); }
        send_str("OK");
    }
}

void parse_dio(const int n, const char *msg)
{
    char rest[MSGLEN];
    bool update = 0;

    if      (equal(msg, "DIR", "ection", "?"))       { send_str(scpi.dio_dir[n] == INPUT ? "INPUT" : "OUTPUT"); }
    else if (start(msg, "DIR", "ection", " ", rest))
    {
        if      (equal(rest, "IN", "put"))           { scpi.dio_dir[n] = INPUT;  update = 1;                    }
        else if (equal(rest, "OUT", "put"))          { scpi.dio_dir[n] = OUTPUT; update = 1;                    }
        else                                         { send_eps(EPA_REPLY_INVALID_ARG);                         }
    }
    else if (equal(msg, "INV", "ert", "?"))          { send_hex(scpi.dio_invert[n]);                            }
    else if (start(msg, "INV", "ert", " ",    rest))
    {
        if      (equal(rest, "1"))                   { scpi.dio_invert[n] = 1;   update = 1;                    }
        else if (equal(rest, "0"))                   { scpi.dio_invert[n] = 0;   update = 1;                    }
        else                                         { send_eps(EPA_REPLY_INVALID_ARG);                         }
    }
    else if (start(msg, "IN", "put",  ":",    rest)) { parse_input(n, rest);                                    }
    else if (start(msg, "OUT", "put", ":",    rest)) { parse_output(n, rest);                                   }
    else if (equal(msg, "VAL", "ue", "?"))           { send_hex(read_input(n));                                 }  // also works for output pins (TODO: confirm this)
    else if (start(msg, "VAL", "ue", " ",     rest))
    {
        if (scpi.dio_dir[n] == OUTPUT) 
        {
            if      (equal(rest, "1"))               { scpi.dio_setval[n] = 1;   update = 1;                    }
            else if (equal(rest, "0"))               { scpi.dio_setval[n] = 0;   update = 1;                    }
            else                                     { send_eps(EPA_REPLY_INVALID_ARG);                         }
        }
        else                                         { send_eps(EPA_REPLY_READONLY);                            }
    }
    else                                             { send_eps(EPA_REPLY_INVALID_CMD);                         }

    if (update)
    {
        update_dio(n);
        send_str("OK");
    }
}

void parse_input(const int n, const char *msg)
{
    char rest[MSGLEN];
    bool update = 0;

    if      (equal(msg, "PULL", "up", "?"))       { send_hex(scpi.dio_pullup[n]);       }
    else if (start(msg, "PULL", "up", " ", rest))
    {
        if      (equal(rest, "1"))                { scpi.dio_pullup[n] = 1; update = 1; }
        else if (equal(rest, "0"))                { scpi.dio_pullup[n] = 0; update = 1; }
        else                                      { send_eps(EPA_REPLY_INVALID_ARG);    }
    }
    else if (equal(msg, "VAL", "ue", "?"))
    {
        if (scpi.dio_dir[n] == INPUT)             { send_hex(read_input(n));            }
        else                                      { send_eps(EPA_REPLY_NA);             }
    }
    else if (start(msg, "VAL", "ue", " ",  rest)) { send_eps(EPA_REPLY_READONLY);       }
    else                                          { send_eps(EPA_REPLY_INVALID_CMD);    }

    if (update)
    {
        update_dio(n);
        send_str("OK");
    }
}

void parse_output(const int n, const char *msg)
{
    char rest[MSGLEN];
    bool update = 0;

    if      (equal(msg, "VAL", "ue", "?"))       { send_hex(scpi.dio_setval[n]);       }
    else if (start(msg, "VAL", "ue", " ", rest))
    {
        if      (equal(rest, "1"))               { scpi.dio_setval[n] = 1; update = 1; }
        else if (equal(rest, "0"))               { scpi.dio_setval[n] = 0; update = 1; }
        else                                     { send_eps(EPA_REPLY_INVALID_ARG);    }
    }
    else                                         { send_eps(EPA_REPLY_INVALID_CMD);    }

    if (update)
    {
        if (scpi.dio_dir[n] == OUTPUT) { update_dio(n); }
        send_str("OK");
    }
}

void parse_system(const char *msg)
{
    char rest[MSGLEN];

    if (equal(msg, "REB", "oot"))
    {
        send_eps(EPA_REPLY_REBOOTING);
        wdt_enable(WDTO_1S);
        while(1);
    }
    else if (start(msg, "COMM", "unicate", ":LAN:", rest)) { parse_lan(rest);                 }
    else                                                   { send_eps(EPA_REPLY_INVALID_CMD); }
}

void parse_lan(const char *msg)
{
    char rest[MSGLEN];
    bool update = 0;

    SCPI_LAN scpi_lan;
    EEPROM.get(EPA_SCPI_LAN, scpi_lan);

    if (equal(msg, "MOD", "e", "?"))
    {
        send_lan(scpi_lan.mode, NOEOL);
#ifdef LAN
        send_str(" (ACTUAL: ",  NOEOL);
        send_lan(scpi_lan_mode, NOEOL);
        send_str(")");
#else
        send_str(" (ACTUAL: NOT AVAILABLE)");
#endif
    }
    else if (start(msg, "MOD", "e", " ", rest))
    {
        if      (equal(rest, "OFF"))          { scpi_lan.mode = LAN_OFF;    update = 1;                                }
        else if (equal(rest, "DHCP"))         { scpi_lan.mode = LAN_DHCP;   update = 1;                                }
        else if (equal(rest, "STAT", "ic"))   { scpi_lan.mode = LAN_STATIC; update = 1;                                }
        else                                  { send_eps(EPA_REPLY_INVALID_ARG);                                       }

    }
    else if (equal(msg, "MAC?"))              { send_mac(scpi_lan.mac);                                                }
    else if (start(msg, "MAC ", rest))
    {
        if (parse_mac(rest, scpi_lan.mac))    { update = 1;                                                            }
        else                                  { send_eps(EPA_REPLY_INVALID_ARG);                                       }
    }
    else if (start(msg, "IP",          rest)) { parse_lan_ip(rest, scpi_lan_ip,      scpi_lan.ip_static,      update); }
    else if (start(msg, "GATE", "way", rest)) { parse_lan_ip(rest, scpi_lan_gateway, scpi_lan.gateway_static, update); }
    else if (start(msg, "SUB", "net",  rest)) { parse_lan_ip(rest, scpi_lan_subnet,  scpi_lan.subnet_static,  update); }
    else                                      { send_eps(EPA_REPLY_INVALID_CMD);                                       }

    if (update)
    {
        EEPROM.put(EPA_SCPI_LAN, scpi_lan);  // save immediately
        send_eps(EPA_REPLY_REBOOT_REQ);
    }
}

void parse_lan_ip(const char *msg, const uint32_t addr, uint32_t &addr_static, bool &update)
{
    char rest[MSGLEN];

    if      (equal(msg, "?"))                      { send_ip(addr);                   }
    else if (start(msg, " ", rest))                { send_eps(EPA_REPLY_READONLY);    }
    else if (equal(msg, ":STAT", "ic", "?"))       { send_ip(addr_static);            }
    else if (start(msg, ":STAT", "ic", " ", rest))
    {
        if (parse_ip(rest, addr_static))           { update = 1;                      }
        else                                       { send_eps(EPA_REPLY_INVALID_ARG); }
    }
    else                                           { send_eps(EPA_REPLY_INVALID_CMD); }
}
