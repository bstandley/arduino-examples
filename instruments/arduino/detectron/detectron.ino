#define LAN
#define MSGLEN 64  // includes null-terminator
#define PORT   18

#include <avr/wdt.h>
#include "eeprom/shared.h"  // includes EEPROM.h
#include "comm.h"           // includes Ethernet.h, if enabled
#include "parse.h"

const unsigned long conf_commit           = 0x1234abc;  // edit to match current commit before compile/download!
const unsigned int  conf_dhcp_cycles      = 1000;
const byte          conf_y_all            = 0x7F;       // binary 01111111, number of ones must match NCHAN!
const byte          conf_input_pin[NCHAN] = {9, 8, 7, 6, 5, 3, 2};

// SCPI commands:
//   *IDN?           model and version
//   *TRG            simulate events on all enabled inputs
//   *SAV            save settings to EEPROM (LAN excluded)
//   *RCL            recall EEPROM settings (also performed on startup) (LAN excluded)
//   *RST            reset to default settings (LAN excluded)
//   :SYSTem:REBoot  reboot the device

// persistent SCPI settings:
SCPI scpi;

// runtime SCPI variables (read-only except where noted):
long     scpi_input_count[NCHAN];  // :INput<n>:COUNt                  hardware events detected since reboot
#ifdef LAN
byte     scpi_lan_mode;            // :SYSTem:COMMunicate:LAN:MODe     actual mode (writes go to scpi_lan.mode)
#endif
uint32_t scpi_lan_ip;              // :SYSTem:COMMunicate:LAN:IP       current ip address
uint32_t scpi_lan_gateway;         // :SYSTem:COMMunicate:LAN:GATEway  current gateway address
uint32_t scpi_lan_subnet;          // :SYSTem:COMMunicate:LAN:SUBnet   current subnet mask

bool x_old[NCHAN];

#ifdef LAN
EthernetServer server(PORT);
EthernetUDP udp;
unsigned int countdown_dhcp;
#endif

void setup()
{
    wdt_disable();  // just in case the bootloader does not do this automatically

    EEPROM.get(EPA_SCPI, scpi);

    for (int n = 0; n < NCHAN; n++)
    {
        pinMode(conf_input_pin[n], INPUT_PULLUP);  // TODO: make configurable
        scpi_input_count[n] = 0;
    }
    update_inputs(x_old);

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

    bool x_new[NCHAN];
    update_inputs(x_new);

    bool event = 0;
    for (int n = 0; n < NCHAN; n++)
    {
        if (x_old[n] != x_new[n] && ((scpi.input_mode[n] == RISING  && x_new[n]) ||
                                     (scpi.input_mode[n] == FALLING && x_old[n]) ||
                                     (scpi.input_mode[n] == CHANGE)))
        {
            scpi_input_count[n]++;
            event = 1;
        }
    }

    if (event) { send_event(pack_bits(x_old), pack_bits(x_new)); }

    for (int n = 0; n < NCHAN; n++) { x_old[n] = x_new[n]; }

    delay(1);
}

byte pack_bits(const bool *x)
{
    byte rv = 0;
    for (int n = 0; n < NCHAN; n++) { rv |= x[n] << n; }
    return rv;
}

void send_event(const byte y_old, const byte y_new)
{
    if (scpi.output_serial)
    {
        Serial.write(y_old);
        Serial.write(y_new);
        Serial.println();
    }

    if (scpi.output_udp)
    {
        udp.beginPacket(scpi.output_udp_dest, scpi.output_udp_port);
        udp.write(y_old);
        udp.write(y_new);
        udp.endPacket();
    }
}

void sim_events()
{
    for (int n = 0; n < NCHAN; n++)
    {
        if (scpi.input_mode[n] & RISING)
        {
            send_event(0, conf_y_all);
            break;
        }
    }

    for (int n = 0; n < NCHAN; n++)
    {
        if (scpi.input_mode[n] & FALLING)
        {
            send_event(conf_y_all, 0);
            break;
        }
    }
}

// runtime update functions:

void update_inputs(bool *x)
{
    for (int n = 0; n < NCHAN; n++)
    {
        bool x_raw = digitalRead(conf_input_pin[n]);
        x[n] = scpi.input_invert[n] ? !x_raw : x_raw;
    }
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
    else if (equal(msg, "*TRG"))                   { send_str("OK"); sim_events();               } // reply first, so serial events do not get mixed in with the SCPI conversation
    else if (equal(msg, "*SAV"))                   { EEPROM.put(EPA_SCPI, scpi); send_str("OK"); }
    else if (equal(msg, "*RCL"))                   { EEPROM.get(EPA_SCPI, scpi); update = 1;     }
    else if (equal(msg, "*RST"))                   { scpi_default(scpi);         update = 1;     }
    else if (start(msg, ":IN", "put", "1:", rest)) { parse_input(0, rest);                       }
    else if (start(msg, ":IN", "put", "2:", rest)) { parse_input(1, rest);                       }
    else if (start(msg, ":IN", "put", "3:", rest)) { parse_input(2, rest);                       }
    else if (start(msg, ":IN", "put", "4:", rest)) { parse_input(3, rest);                       }
    else if (start(msg, ":IN", "put", "5:", rest)) { parse_input(4, rest);                       }
    else if (start(msg, ":IN", "put", "6:", rest)) { parse_input(5, rest);                       }
    else if (start(msg, ":IN", "put", "7:", rest)) { parse_input(6, rest);                       }
    else if (start(msg, ":OUT", "put", ":", rest)) { parse_output(rest);                         }
    else if (start(msg, ":SYST", "em", ":", rest)) { parse_system(rest);                         }
    else                                           { send_eps(EPA_REPLY_INVALID_CMD);            }

    if (update)
    {
        update_inputs(x_old);
        send_str("OK");
    }
}

void parse_input(const int n, const char *msg)
{
    char rest[MSGLEN];
    bool update = 0;

    if (equal(msg, "MOD", "e", "?"))
    {
        send_str(scpi.input_mode[n] == RISING  ? "RISING"  :
                 scpi.input_mode[n] == FALLING ? "FALLING" :
                 scpi.input_mode[n] == CHANGE  ? "CHANGE"  :
                                                 "OFF");
    }
    else if (start(msg, "MOD", "e", " ",   rest))
    {
        if      (equal(rest, "OFF"))              { scpi.input_mode[n] = 0;       send_str("OK"); }
        else if (equal(rest, "RIS", "ing"))       { scpi.input_mode[n] = RISING;  send_str("OK"); }
        else if (equal(rest, "FALL", "ing"))      { scpi.input_mode[n] = FALLING; send_str("OK"); }
        else if (equal(rest, "CHA", "nge"))       { scpi.input_mode[n] = CHANGE;  send_str("OK"); }
        else                                      { send_eps(EPA_REPLY_INVALID_ARG);              }
    }
    else if (equal(msg, "INV", "ert", "?"))       { send_hex(scpi.input_invert[n]);               }
    else if (start(msg, "INV", "ert", " ", rest))
    {
        if      (equal(rest, "1"))                { scpi.input_invert[n] = 1; update = 1;         }
        else if (equal(rest, "0"))                { scpi.input_invert[n] = 0; update = 1;         }
        else                                      { send_eps(EPA_REPLY_INVALID_ARG);              }
    }
    else if (equal(msg, "COUN", "t", "?"))        { send_int(scpi_input_count[n]);                }
    else if (start(msg, "COUN", "t", " ",  rest)) { send_eps(EPA_REPLY_READONLY);                 }
    else                                          { send_eps(EPA_REPLY_INVALID_CMD);              }

    if (update)
    {
        update_inputs(x_old);
        send_str("OK");
    }
}

void parse_output(const char *msg)
{
    char rest[MSGLEN];

    if      (start(msg, "SER", "ial", ":", rest)) { parse_serial(rest);              }
    else if (start(msg, "UDP:",            rest)) { parse_udp(rest);                 }
    else                                          { send_eps(EPA_REPLY_INVALID_CMD); }
}

void parse_serial(const char *msg)
{
    char rest[MSGLEN];

    if      (equal(msg, "EN", "able", "?"))       { send_hex(scpi.output_serial);           }
    else if (start(msg, "EN", "able", " ", rest))
    {
        if      (equal(rest, "1"))                { scpi.output_serial = 1; send_str("OK"); }
        else if (equal(rest, "0"))                { scpi.output_serial = 0; send_str("OK"); }
        else                                      { send_eps(EPA_REPLY_INVALID_ARG);        }
    }
    else                                          { send_eps(EPA_REPLY_INVALID_CMD);        }
}

void parse_udp(const char *msg)
{
    char rest[MSGLEN];

    if      (equal(msg, "EN", "able", "?"))            { send_hex(scpi.output_udp);           }
    else if (start(msg, "EN", "able", " ",       rest))
    {
        if      (equal(rest, "1"))                     { scpi.output_udp = 1; send_str("OK"); }
        else if (equal(rest, "0"))                     { scpi.output_udp = 0; send_str("OK"); }
        else                                           { send_eps(EPA_REPLY_INVALID_ARG);     }
    }
    else if (equal(msg, "DEST", "ination", "?"))       { send_ip(scpi.output_udp_dest);       }
    else if (start(msg, "DEST", "ination", " ", rest))
    {
        if (parse_ip(rest, scpi.output_udp_dest))      { send_str("OK");                      }
        else                                           { send_eps(EPA_REPLY_INVALID_ARG);     }
    }
    else if (equal(msg, "PORT?"))                      { send_int(scpi.output_udp_port);      }
    else if (start(msg, "PORT ",                rest))
    {
        if (parse_port(rest, scpi.output_udp_port))    { send_str("OK");                      }
        else                                           { send_eps(EPA_REPLY_INVALID_ARG);     }
    }
    else                                               { send_eps(EPA_REPLY_INVALID_CMD);     }
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
