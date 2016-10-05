#define LAN
#define MSGLEN 64  // includes null-terminator
#define PORT   18

#include <avr/wdt.h>
#include "eeprom/shared.h"  // includes EEPROM.h
#include "comm.h"           // includes Ethernet.h, if enabled
#include "parse.h"

const unsigned long conf_commit             = 0x1234abc;  // edit to match current commit before compile/download!
const long          conf_clock_freq_int     = 2000000;    // board-dependent, assumes prescaler set to /8
const byte          conf_clock_pin          = 12;         // board-dependent
const byte          conf_trig_pin           = 2;          // pin must support low-level interrupts
const unsigned int  conf_start_us           = 10;
const unsigned int  conf_dhcp_cycles        = 1000;
const unsigned long conf_measure_ms         = 500;
const byte          conf_pulse_pin  [NCHAN] = {8, 7, 6, 5};
byte *              conf_pulse_port [NCHAN] = {&PORTB, &PORTE, &PORTD, &PORTC};  // board-dependent, must match pulse_pin
const byte          conf_pulse_mask [NCHAN] = {1 << 4, 1 << 6, 1 << 7, 1 << 6};  // board dependent, must match pulse_pin

// SCPI commands:
//   *IDN?                       model and version
//   *TRG                        soft trigger, independent of :TRIG:ARMed
//   *SAV                        save settings to EEPROM (LAN excluded)
//   *RCL                        recall EEPROM settings (also performed on startup) (LAN excluded)
//   *RST                        reset to default settings (LAN excluded)
//   :CLOCK:FREQ:INTernal        ideal internal frequency in Hz
//   :CLOCK:FREQ:MEASure         measured frequency in Hz of currently-configured clock
//   :SYSTem:REBoot              reboot the device
//   :SYSTem:COMMunicate:LAN:IP  current ip address

// persistent SCPI settings:
SCPI scpi;

// runtime SCPI variables (read-only except where noted):
long          scpi_clock_freq;          // :CLOCK:FREQuency              ideal frequency in Hz of currently-configure clock
volatile long scpi_trig_count;          // :TRIGger:COUNt                hardware triggers detected since reboot
volatile bool scpi_trig_armed;          // :TRIGger:ARMed                armed (read/write)
volatile bool scpi_trig_ready;          // :TRIGger:READY                ready (armed plus at least one valid channel)
bool          scpi_pulse_valid[NCHAN];  // :PULSe<n>:VALid               output channel has valid/usable pulse sequence
#ifdef LAN
byte          scpi_lan_mode;            // :SYSTem:COMMunicate:LAN:MODE  actual mode (writes go to scpi_lan.mode)
#endif

unsigned long          k_delay  [NCHAN];
unsigned long          k_width  [NCHAN];
unsigned long          k_period [NCHAN];
unsigned long          k_end    [NCHAN];
volatile unsigned long k_next   [NCHAN];
volatile bool          x_next   [NCHAN];
volatile int           N_active;
volatile unsigned long k_cur;
volatile unsigned int  c_cur;

#ifdef LAN
EthernetServer server(PORT);
unsigned int countdown_dhcp;
#endif

void setup()
{
    wdt_disable();  // just in case the bootloader does not do this automatically

    EEPROM.get(EPA_SCPI, scpi);

    pinMode(conf_clock_pin, INPUT);
    update_clock();  // also initialize scpi_clock_freq

    for (int n = 0; n < NCHAN; n++)
    {
        pinMode(conf_pulse_pin[n], OUTPUT);
        update_pulse(n);  // also initialize scpi_pulse_valid[n]
    }

    pinMode(conf_trig_pin, INPUT);
    scpi_trig_count = 0;
    scpi_trig_armed = scpi.trig_rearm;
    update_trig_ready();  // also initialize scpi_trig_ready
    update_trig_edge();  // actually configure interrupt

    Serial.begin(9600);

#ifdef LAN
    SCPI_LAN scpi_lan;
    EEPROM.get(EPA_SCPI_LAN, scpi_lan);

    scpi_lan_mode = scpi_lan.mode;
    if (scpi_lan_mode == LAN_DHCP)
    {
        Ethernet.begin(scpi_lan.mac);
        countdown_dhcp = conf_dhcp_cycles;
    }
    else if (scpi_lan_mode == LAN_STATIC)
    {
        const byte dns[4] = {0, 0, 0, 0};  // DNS is not used
        Ethernet.begin(scpi_lan.mac, scpi_lan.ip_static, dns, scpi_lan.gateway_static, scpi_lan.subnet_static);
    }
#endif
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
            countdown_dhcp = conf_dhcp_cycles;
        }
        else { countdown_dhcp--; }
    }
#endif

    delay(1);
}

void run_null() { return; }

void run_hw_trig()
{
    c_cur = TCNT1;        // TESTING: read as early as possible to make global timebase as accurate as possible
    if (scpi_trig_ready)  // TESTING: precalculated to save a bit of time
    {
        gen_pulses();
        scpi_trig_armed = scpi.trig_rearm;
        update_trig_ready();
    }
    scpi_trig_count++;
}

void run_sw_trig()  // subset of run_hw_trig() ignoring armed/disarmed and trigger count
{
    c_cur = TCNT1;
    if (N_active > 0)
    {
        gen_pulses();
        update_trig_ready();
    }
}

void gen_pulses()
{
    for (int n = 0; n < NCHAN; n++)  // TESTING: quick iteration to accelerate short delays
    {
        if (k_cur >= k_next[n])  // use x_next[n] == 1 below:
        {
            pulse_write(n, !scpi.pulse_invert[n]);

            k_next[n] += k_width[n];
            x_next[n] = 0;
        }
    }
    
    scpi_trig_ready = 0;  // ok to set now  TODO: test against spurious triggers

    while (1)
    {
        unsigned int c_diff = TCNT1 - c_cur;
        c_cur += c_diff;
        k_cur += c_diff;

        for (int n = 0; n < NCHAN; n++)  // full loop
        {
            if (k_cur >= k_next[n])
            {
                pulse_write(n, x_next[n] ? !scpi.pulse_invert[n] : scpi.pulse_invert[n]);

                k_next[n] += x_next[n] ? k_width[n] : (k_period[n] - k_width[n]);
                x_next[n] = !x_next[n];

                if (k_next[n] >= k_end[n])
                {
                    pulse_write(n, scpi.pulse_invert[n]);
                    k_next[n] = 4100000000;
                    N_active--;
                    if (N_active == 0) { return; }
                }
            }
        }
    }
}

void pulse_write(const int n, const bool x)  // TESTING: a bit quicker than digitalWrite()
{
    if (x) { *conf_pulse_port[n] |=  conf_pulse_mask[n]; }
    else   { *conf_pulse_port[n] &= ~conf_pulse_mask[n]; }
}

// runtime update functions:

bool update_clock()
{
    scpi_clock_freq = (scpi.clock_src == INTERNAL) ? conf_clock_freq_int : scpi.clock_freq_ext;

    TCCR1A = 0x0;                                  // COM1A1=0 COM1A0=0 COM1B1=0 COM1B0=0 FOC1A=0 FOC1B=0 WMG11=0 WGM10=0
    TCCR1B = (scpi.clock_src == INTERNAL) ? 0x2 :  // ICNC1=0  ICES1=0  n/a=0    WGM13=0  WGM12=0 CS12=0  CS11=1  CS10=0  (internal, /8)
             (scpi.clock_edge == RISING)  ? 0x7 :  // ICNC1=0  ICES1=0  n/a=0    WGM13=0  WGM12=0 CS12=1  CS11=1  CS10=1  (external, rising)
                                            0x6;   // ICNC1=0  ICES1=0  n/a=0    WGM13=0  WGM12=0 CS12=1  CS11=1  CS10=0  (external, falling)
}

void update_trig_edge()
{
    attachInterrupt(digitalPinToInterrupt(conf_trig_pin), run_null,    scpi.trig_edge);
    delay(100);  // let possibly lingering interrupt clear out
    attachInterrupt(digitalPinToInterrupt(conf_trig_pin), run_hw_trig, scpi.trig_edge);
}

bool update_pulse(const int n)
{
    pulse_write(n, scpi.pulse_invert[n]);  // if inverting, then set initial value HIGH	

    float scpi_clock_freq_MHz = float(scpi_clock_freq) / 1e6;

    k_delay[n]  = scpi_clock_freq_MHz * scpi.pulse_delay[n];
    k_width[n]  = scpi_clock_freq_MHz * min(scpi.pulse_width[n], scpi.pulse_period[n]);
    k_period[n] = scpi_clock_freq_MHz * scpi.pulse_period[n];
    k_end[n]    = k_delay[n] + k_period[n] * scpi.pulse_cycles[n];

    return scpi_pulse_valid[n] = (scpi_clock_freq_MHz * (float(scpi.pulse_delay[n]) + float(scpi.pulse_period[n]) * scpi.pulse_cycles[n]) < 4e9);  // calculate with floats
}

void update_trig_ready()
{
    k_cur = (scpi_clock_freq * conf_start_us) / 1000000;  // account for interrupt processing time
    N_active = 0;

    for (int n = 0; n < NCHAN; n++)
    {
        if (scpi_pulse_valid[n] && (scpi.pulse_cycles[n] > 0))
        {
            N_active++;
            k_next[n] = k_delay[n];
            x_next[n] = 1;
        }
        else { k_next[n] = 4100000000; }
    }

    scpi_trig_ready = scpi_trig_armed && N_active > 0;
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
    else if (equal(msg, "*TRG"))                    { run_sw_trig();              send_str("OK"); }
    else if (equal(msg, "*SAV"))                    { EEPROM.put(EPA_SCPI, scpi); send_str("OK"); }
    else if (equal(msg, "*RCL"))                    { EEPROM.get(EPA_SCPI, scpi); update = 1;     }
    else if (equal(msg, "*RST"))                    { scpi_default(scpi);         update = 1;     }
    else if (start(msg, ":CLOCK:",           rest)) { parse_clock(rest);                          }
    else if (start(msg, ":TRIG", "ger", ":", rest)) { parse_trig(rest);                           }
    else if (start(msg, ":PULS", "E",  "1:", rest)) { parse_pulse(0, rest);                       }
    else if (start(msg, ":PULS", "E",  "2:", rest)) { parse_pulse(1, rest);                       }
    else if (start(msg, ":PULS", "E",  "3:", rest)) { parse_pulse(2, rest);                       }
    else if (start(msg, ":PULS", "E",  "4:", rest)) { parse_pulse(3, rest);                       }
    else if (start(msg, ":SYST", "EM",  ":", rest)) { parse_system(rest);                         }
    else                                            { send_eps(EPA_REPLY_INVALID_CMD);            }

    if (update)
    {
        update_clock();
        bool ok = 1;
        for (int n = 0; n < NCHAN; n++) { if (!update_pulse(n)) { ok = 0; } }  // at least one channel might be not ok . . .
        update_trig_ready();
        update_trig_edge();

        if (ok) { send_str("OK");            }
        else    { send_eps(EPA_REPLY_CHECK); }
    }
}

void parse_clock(const char *msg)
{
    char rest[MSGLEN];
    bool update = 0;

    if      (equal(msg, "SRC?"))                     { send_str(scpi.clock_src == INTERNAL ? "INTERNAL" : "EXTERNAL"); }
    else if (start(msg, "SRC ", rest))
    {
        if      (equal(rest, "INT", "ernal"))        { scpi.clock_src = INTERNAL; update = 1;                          }
        else if (equal(rest, "EXT", "ernal"))        { scpi.clock_src = EXTERNAL; update = 1;                          }
        else                                         { send_eps(EPA_REPLY_INVALID_ARG);                                }

    }
    else if (start(msg, "EDGE", rest))               { parse_edge(rest, scpi.clock_edge, update);                      }
    else if (equal(msg, "FREQ", "uency", "?"))       { send_int(scpi_clock_freq);                                      }
    else if (start(msg, "FREQ", "uency", " ", rest)) { send_eps(EPA_REPLY_READONLY);                                   }
    else if (start(msg, "FREQ", "uency", ":", rest)) { parse_clock_freq(rest, update);                                 }
    else                                             { send_eps(EPA_REPLY_INVALID_CMD);                                }

    if (update)
    {
        update_clock();
        bool ok = 1;
        for (int n = 0; n < NCHAN; n++) { if (!update_pulse(n)) { ok = 0; } }  // at least one channel might be not ok . . .
        update_trig_ready();

        if (ok) { send_str("OK");            }
        else    { send_eps(EPA_REPLY_CHECK); }
    }
}

void parse_edge(const char *msg, byte &edge, bool &update)
{
    char rest[MSGLEN];

    if      (equal(msg, "?"))                { send_str(edge == RISING ? "RISING" : "FALLING"); }
    else if (start(msg, " ", rest))
    {
        if      (equal(rest, "RIS", "ing"))  { edge = RISING;  update = 1;                      }
        else if (equal(rest, "FALL", "ing")) { edge = FALLING; update = 1;                      }
        else                                 { send_eps(EPA_REPLY_INVALID_ARG);                 }
    }
    else                                     { send_eps(EPA_REPLY_INVALID_CMD);                 }
}

void parse_clock_freq(const char *msg, bool &update)
{
    char rest[MSGLEN];

    if (equal(msg, "MEAS", "ure", "?"))
    {
        unsigned long t = millis();
        unsigned int  c = TCNT1;
        long          k = 0;

        while (millis() - t <= conf_measure_ms)
        {
            unsigned int c_diff = TCNT1 - c;
            c += c_diff;
            k += c_diff;
        }

        send_int((1000 * k) / conf_measure_ms);
    }
    else if (start(msg, "MEAS", "ure",  " ", rest))         { send_eps(EPA_REPLY_READONLY);    }
    else if (equal(msg, "INT", "ernal", "?"))               { send_int(conf_clock_freq_int);   }
    else if (start(msg, "INT", "ernal", " ", rest))         { send_eps(EPA_REPLY_READONLY);    }
    else if (equal(msg, "EXT", "ernal", "?"))               { send_int(scpi.clock_freq_ext);   }
    else if (start(msg, "EXT", "ernal", " ", rest))
    {
        if (parse_num(rest, scpi.clock_freq_ext, ZERO_NOK)) { update = 1;                      }
        else                                                { send_eps(EPA_REPLY_INVALID_ARG); }
    }
    else                                                    { send_eps(EPA_REPLY_INVALID_CMD); }
}

void parse_trig(const char *msg)
{
    char rest[MSGLEN];
    bool update = 0;

    if      (start(msg, "EDGE", rest))           { parse_edge(rest, scpi.trig_edge, update); }
    else if (equal(msg, "ARM", "ed", "?"))       { send_hex(scpi_trig_armed);                }
    else if (start(msg, "ARM", "ed", " ", rest))
    {
        if      (equal(rest, "1"))               { scpi_trig_armed = 1; update = 1;          }
        else if (equal(rest, "0"))               { scpi_trig_armed = 0; update = 1;          }
        else                                     { send_eps(EPA_REPLY_INVALID_ARG);          }

    }
    else if (equal(msg, "READY?"))               { send_hex(scpi_trig_ready);                }
    else if (start(msg, "READY ", rest))         { send_eps(EPA_REPLY_READONLY);             }
    else if (equal(msg, "REARM?"))               { send_hex(scpi.trig_rearm);                }
    else if (start(msg, "REARM ", rest))
    {
        if      (equal(rest, "1"))               { scpi.trig_rearm = 1; send_str("OK");      }
        else if (equal(rest, "0"))               { scpi.trig_rearm = 0; send_str("OK");      }
        else                                     { send_eps(EPA_REPLY_INVALID_ARG);          }
    }
    else if (equal(msg, "COUN", "t", "?"))       { send_int(scpi_trig_count);                }
    else if (start(msg, "COUN", "t", " ", rest)) { send_eps(EPA_REPLY_READONLY);             }
    else                                         { send_eps(EPA_REPLY_INVALID_CMD);          }

    if (update)
    {
        update_trig_ready();  // always ok
        update_trig_edge();   // always ok
        send_str("OK");
    }
}

void parse_pulse(const int n, const char *msg)
{
    char rest[MSGLEN];
    bool update = 0;

    if      (equal(msg, "DEL", "ay", "?"))                      { send_micros(scpi.pulse_delay[n]);     }
    else if (start(msg, "DEL", "ay", " ",  rest))
    {
        if (parse_micros(rest, scpi.pulse_delay[n], ZERO_OK))   { update = 1;                           }
        else                                                    { send_eps(EPA_REPLY_INVALID_ARG);      }
    }
    else if (equal(msg, "WID", "th", "?"))                      { send_micros(scpi.pulse_width[n]);     }
    else if (start(msg, "WID", "th", " ",  rest))
    {
        if (parse_micros(rest, scpi.pulse_width[n], ZERO_NOK))  { update = 1;                           }
        else                                                    { send_eps(EPA_REPLY_INVALID_ARG);      }
    }
    else if (equal(msg, "PER", "iod", "?"))                     { send_micros(scpi.pulse_period[n]);    }
    else if (start(msg, "PER", "iod", " ", rest))
    {
        if (parse_micros(rest, scpi.pulse_period[n], ZERO_NOK)) { update = 1;                           }
        else                                                    { send_eps(EPA_REPLY_INVALID_ARG);      }
    }
    else if (equal(msg, "CYC", "les", "?"))                     { send_int(scpi.pulse_cycles[n]);       }
    else if (start(msg, "CYC", "les", " ", rest))
    {
        if (parse_num(rest, scpi.pulse_cycles[n], ZERO_OK))     { update = 1; }
        else                                                    { send_eps(EPA_REPLY_INVALID_ARG);      }
    }
    else if (equal(msg, "INV", "ert", "?"))                     { send_hex(scpi.pulse_invert[n]);       }
    else if (start(msg, "INV", "ert", " ", rest))
    {
        if      (equal(rest, "1"))                              { scpi.pulse_invert[n] = 1; update = 1; }
        else if (equal(rest, "0"))                              { scpi.pulse_invert[n] = 0; update = 1; }
        else                                                    { send_eps(EPA_REPLY_INVALID_ARG);      }
    }
    else if (equal(msg, "VAL", "id", "?"))                      { send_hex(scpi_pulse_valid[n]);        }
    else if (start(msg, "VAL", "id", " ",  rest))               { send_eps(EPA_REPLY_READONLY);         }
    else                                                        { send_eps(EPA_REPLY_INVALID_CMD);      }

    if (update)
    {
        bool ok = update_pulse(n);
        update_trig_ready();

        if (ok) { send_str("OK");            }
        else    { send_eps(EPA_REPLY_CHECK); }
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

    if (equal(msg, "MODE?"))
    {
        send_str(scpi_lan.mode == LAN_OFF ? "OFF" : (scpi_lan.mode == LAN_DHCP ? "DHCP" : "STATIC"), NOEOL);
        send_str(" (ACTUAL: ",                                                                       NOEOL);
#ifdef LAN
        send_str(scpi_lan_mode == LAN_OFF ? "OFF" : (scpi_lan_mode == LAN_DHCP ? "DHCP" : "STATIC"), NOEOL);
#else
        send_str("NOT AVAILABLE",                                                                    NOEOL);
#endif
        send_str(")");
    }
    else if (start(msg, "MODE ", rest))
    {
        if      (equal(rest, "OFF"))               { scpi_lan.mode = LAN_OFF;    update = 1; }
        else if (equal(rest, "DHCP"))              { scpi_lan.mode = LAN_DHCP;   update = 1; }
        else if (equal(rest, "STAT", "ic"))        { scpi_lan.mode = LAN_STATIC; update = 1; }
        else                                       { send_eps(EPA_REPLY_INVALID_ARG);        }

    }
    else if (equal(msg, "MAC?"))                   { send_mac(scpi_lan.mac);                 }
    else if (start(msg, "MAC ", rest))
    {
        if (parse_mac(rest, scpi_lan.mac))         { update = 1;                             }
        else                                       { send_eps(EPA_REPLY_INVALID_ARG);        }
    }
    else if (equal(msg, "IP?"))
    {
        byte addr[4] = {0, 0, 0, 0};
#ifdef LAN
        if (scpi_lan_mode != LAN_OFF)
        {
            IPAddress local_ip = Ethernet.localIP();
            for (int i = 0; i < 4; i++) { addr[i] = local_ip[i]; }
        }
#endif
        send_ip(addr);
    }
    else if (start(msg, "IP ",              rest)) { send_eps(EPA_REPLY_READONLY);                            }
    else if (start(msg, "IP:",              rest)) { parse_lan_static(rest, scpi_lan.ip_static,      update); }
    else if (start(msg, "GATE", "way", ":", rest)) { parse_lan_static(rest, scpi_lan.gateway_static, update); }
    else if (start(msg, "SUB", "net",  ":", rest)) { parse_lan_static(rest, scpi_lan.subnet_static,  update); }
    else                                           { send_eps(EPA_REPLY_INVALID_CMD);                         }

    if (update)
    {
        EEPROM.put(EPA_SCPI_LAN, scpi_lan);  // save immediately
        send_eps(EPA_REPLY_REBOOT_REQ);
    }
}

void parse_lan_static(const char *msg, byte *ip, bool &update)
{
    char rest[MSGLEN];

    if      (equal(msg, "STAT", "ic", "?"))       { send_ip(ip);                     }
    else if (start(msg, "STAT", "ic", " ", rest))
    {
        if (parse_ip(rest, ip))                   { update = 1;                      }
        else                                      { send_eps(EPA_REPLY_INVALID_ARG); }
    }
    else                                          { send_eps(EPA_REPLY_INVALID_CMD); }
}
