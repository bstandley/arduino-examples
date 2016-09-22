#define MSGLEN 64  // includes null-terminator

#include <EEPROM.h>
#include "eeprom/shared.h"
#include "util.h"

const unsigned long conf_commit             = 0x1234abc;  // edit to match current commit before compile/download!
const float         conf_clock_freq_int     = 2e6;        // board-dependent, assumes prescaler set to /8
const byte          conf_clock_pin          = 12;         // board-dependent
const byte          conf_trig_pin           = 2;          // pin must support low-level interrupts
const float         conf_start_us           = 10.0;
const unsigned long conf_measure_ms         = 500;
const byte          conf_pulse_pin  [NCHAN] = {8, 7, 6, 5};
byte *              conf_pulse_port [NCHAN] = {&PORTB, &PORTE, &PORTD, &PORTC};  // board-dependent, must match pulse_pin
const byte          conf_pulse_mask [NCHAN] = {1 << 4, 1 << 6, 1 << 7, 1 << 6};  // board dependent, must match pulse_pin

// SCPI commands:
//   *IDN?                 model and version
//   *TRG                  soft trigger, independent of :TRIG:ARMed
//   *SAV                  save settings to EEPROM
//   *RCL                  recall EEPROM settings (also performed on startup)
//   *RST                  reset to default settings
//   :CLOCK:FREQ:INTernal  ideal internal frequency in Hz
//   :CLOCK:FREQ:MEASure   measured frequency in Hz of currently-configured clock

// persistent SCPI settings:
SCPI scpi;

// runtime SCPI variables (read-only except where noted):
float         scpi_clock_freq;           // :CLOCK:FREQ      ideal frequency in Hz of currently-configure clock
volatile long scpi_trig_count;           // :TRIG:COUNT      hardware triggers detected since reboot
volatile bool scpi_trig_armed;           // :TRIG:ARMed      armed (read/write)
volatile bool scpi_trig_ready;           // :TRIG:READY      ready (armed plus at least one valid channel)
bool          scpi_pulse_valid [NCHAN];  // :PULSe<n>:VALid  output channel has valid/usable pulse sequence

unsigned long          k_delay  [NCHAN];
unsigned long          k_width  [NCHAN];
unsigned long          k_period [NCHAN];
unsigned long          k_end    [NCHAN];
volatile unsigned long k_next   [NCHAN];
volatile bool          x_next   [NCHAN];
volatile int           N_active;
volatile unsigned long k_cur;
volatile unsigned int  c_cur;

void setup()
{
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
}

void loop()
{
    if (Serial.available() > 0)
    {
        char msg[MSGLEN];
        if (recv_msg(msg)) { parse_msg(msg); }
    }

    delay(1);
}

void run_null() { return; }

void run_hw_trig()
{
    c_cur = TCNT1;       // TESTING: read as early as possible to make global timebase as accurate as possible
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

float measure_freq()
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

    return 1e3 * float(k) / float(conf_measure_ms);
}

// runtime update functions:

bool update_clock()
{
    scpi_clock_freq = (scpi.clock_src == INTERNAL) ? conf_clock_freq_int : scpi.clock_freq_ext;

    TCCR1A = 0x0;                                  // COM1A1=0 COM1A0=0 COM1B1=0 COM1B0=0 FOC1A=0 FOC1B=0 WMG11=0 WGM10=0
    TCCR1B = (scpi.clock_src == INTERNAL) ? 0x2 :  // ICNC1=0  ICES1=0  n/a=0    WGM13=0  WGM12=0 CS12=0  CS11=1  CS10=0  (internal, /8)
                                            0x7;   // ICNC1=0  ICES1=0  n/a=0    WGM13=0  WGM12=0 CS12=1  CS11=1  CS10=1  (external rising)
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

    k_delay[n]  = scpi_clock_freq * scpi.pulse_delay[n];
    k_width[n]  = scpi_clock_freq * min(scpi.pulse_width[n], scpi.pulse_period[n]);
    k_period[n] = scpi_clock_freq * scpi.pulse_period[n];
    k_end[n]    = k_delay[n] + k_period[n] * scpi.pulse_cycles[n];

    return scpi_pulse_valid[n] = (scpi_clock_freq * (scpi.pulse_delay[n] + scpi.pulse_period[n] * scpi.pulse_cycles[n]) < 4e9);  // calculate with floats
}

bool update_pulse_all()
{
    bool rv = 1;
    for (int n = 0; n < NCHAN; n++) { if (!update_pulse(n)) { rv = 0; } }
    return rv;
}

void update_trig_ready()
{
    k_cur = scpi_clock_freq * conf_start_us / 1e6;  // account for interrupt processing time
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
    else if (equal(msg, "*TRG"))                  { run_sw_trig();              send_str("OK"); }
    else if (equal(msg, "*SAV"))                  { EEPROM.put(EPA_SCPI, scpi); send_str("OK"); }
    else if (equal(msg, "*RCL"))                  { EEPROM.get(EPA_SCPI, scpi); update = 1;     }
    else if (equal(msg, "*RST"))                  { scpi_default(scpi);         update = 1;     }
    else if (start(msg, ":CLOCK",          rest)) { parse_clock(rest);                          }
    else if (start(msg, ":TRIG",           rest)) { parse_trig(rest);                           }
    else if (start(msg, ":PULSE", ":PULS", rest)) { parse_pulse(rest);                          }
    else                                          { send_eps(EPA_REPLY_INVALID_CMD);            }

    if (update)
    {
        update_clock();
        bool ok = update_pulse_all();  // at least one channel might be not ok . . .
        update_trig_ready();
        update_trig_edge();

        if (ok) { send_str("OK");            }
        else    { send_eps(EPA_REPLY_CHECK); }
        
    }
}

void parse_clock(const char *rest)
{
    char arg[MSGLEN];
    bool update = 0;

    if      (equal(rest, ":SRC?"))                                { send_str(scpi.clock_src == INTERNAL ? "INTERNAL" : "EXTERNAL"); }
    else if (start(rest, ":SRC ", arg))
    {
        if      (equal(arg, "INTERNAL", "INT"))                   { scpi.clock_src = INTERNAL; update = 1;                          }
        else if (equal(arg, "EXTERNAL", "EXT"))                   { scpi.clock_src = EXTERNAL; update = 1;                          }
        else                                                      { send_eps(EPA_REPLY_INVALID_ARG);                                }

    }
    else if (equal(rest, ":FREQ?"))                               { send_num(scpi_clock_freq);                                      }
    else if (start(rest, ":FREQ ",                         NULL)) { send_eps(EPA_REPLY_READONLY);                                   }
    else if (equal(rest, ":FREQ:MEASURE?",  ":FREQ:MEAS?"))       { send_num(measure_freq());                                       }
    else if (start(rest, ":FREQ:MEASURE ",  ":FREQ:MEAS ", NULL)) { send_eps(EPA_REPLY_READONLY);                                   }
    else if (equal(rest, ":FREQ:INTERNAL?", ":FREQ:INT?"))        { send_num(conf_clock_freq_int);                                  }
    else if (start(rest, ":FREQ:INTERNAL ", ":FREQ:INT ",  NULL)) { send_eps(EPA_REPLY_READONLY);                                   }
    else if (equal(rest, ":FREQ:EXTERNAL?", ":FREQ:EXT?"))        { send_num(scpi.clock_freq_ext);                                  }
    else if (start(rest, ":FREQ:EXTERNAL ", ":FREQ:EXT ",  arg))
    {
        float value = parse_float(arg, -1.0);
        if (value != -1.0)                                        { scpi.clock_freq_ext = value; update = 1;                        }
        else                                                      { send_eps(EPA_REPLY_INVALID_ARG);                                }
    }
    else                                                          { send_eps(EPA_REPLY_INVALID_CMD);                                }

    if (update)
    {
        update_clock();
        bool ok = update_pulse_all();  // at least one channel might be not ok . . .
        update_trig_ready();

        if (ok) { send_str("OK");            }
        else    { send_eps(EPA_REPLY_CHECK); }
    }
}

void parse_trig(const char *rest)
{
    char arg[MSGLEN];
    bool update = 0;

    if      (equal(rest, ":EDGE?"))                { send_str(scpi.trig_edge == RISING ? "RISING" : "FALLING"); }
    else if (start(rest, ":EDGE ", arg))
    {
        if      (equal(arg, "RISING",  "RIS"))     { scpi.trig_edge = RISING;  update = 1;                      }
        else if (equal(arg, "FALLING", "FALL"))    { scpi.trig_edge = FALLING; update = 1;                      }
        else                                       { send_eps(EPA_REPLY_INVALID_ARG);                           }
    }
    else if (equal(rest, ":ARMED?", ":ARM?"))      { send_num(scpi_trig_armed);                                 }
    else if (start(rest, ":ARMED ", ":ARM ", arg))
    {
        if      (equal(arg, "1"))                  { scpi_trig_armed = 1; update = 1;                           }
        else if (equal(arg, "0"))                  { scpi_trig_armed = 0; update = 1;                           }
        else                                       { send_eps(EPA_REPLY_INVALID_ARG);                           }

    }
    else if (equal(rest, ":READY?"))               { send_num(scpi_trig_ready);                                 }
    else if (start(rest, ":READY ", NULL))         { send_eps(EPA_REPLY_READONLY);                              }
    else if (equal(rest, ":REARM?"))               { send_num(scpi.trig_rearm);                                 }
    else if (start(rest, ":REARM ", arg))
    {
        if      (equal(arg, "1"))                  { scpi.trig_rearm = 1; send_str("OK");                       }
        else if (equal(arg, "0"))                  { scpi.trig_rearm = 0; send_str("OK");                       }
        else                                       { send_eps(EPA_REPLY_INVALID_ARG);                           }
    }
    else if (equal(rest, ":COUNT?"))               { send_num(scpi_trig_count);                                 }
    else if (start(rest, ":COUNT ", NULL))         { send_eps(EPA_REPLY_READONLY);                              }
    else                                           { send_eps(EPA_REPLY_INVALID_CMD);                           }

    if (update)
    {
        update_trig_ready();  // always ok
        update_trig_edge();   // always ok
        send_str("OK");
    }
}

void parse_pulse(const char *rest)
{
    char arg[MSGLEN];
    bool update = 0;

    int n = (rest[0] == '1') ? 0 :
            (rest[0] == '2') ? 1 :
            (rest[0] == '3') ? 2 :
            (rest[0] == '4') ? 3 :
                               -1;  // also handles case where msg == ":PULSE" (or ":PULS"), where rest[0] == 0

    if (n != -1)
    {
        char *rest_p = rest + 1;  // safe because we know strlen(rest) > 0
        if      (equal(rest_p, ":DELAY?", ":DEL?"))       { send_num(scpi.pulse_delay[n]);            }
        else if (start(rest_p, ":DELAY ", ":DEL ",  arg))
        {
            float value = parse_float(arg, -1.0);
            if (value >= 0.0)                             { scpi.pulse_delay[n] = value; update = 1;  }
            else                                          { send_eps(EPA_REPLY_INVALID_ARG);          }
        }
        else if (equal(rest_p, ":WIDTH?", ":WID?"))       { send_num(scpi.pulse_width[n]);            }
        else if (start(rest_p, ":WIDTH ", ":WID ",  arg))
        {
            float value = parse_float(arg, -1.0);
            if (value > 0.0)                              { scpi.pulse_width[n] = value; update = 1;  }
            else                                          { send_eps(EPA_REPLY_INVALID_ARG);          }
        }
        else if (equal(rest_p, ":PERIOD?", ":PER?"))      { send_num(scpi.pulse_period[n], 6);        }
        else if (start(rest_p, ":PERIOD ", ":PER ", arg))
        {
            float value = parse_float(arg, -1.0);
            if (value > 0.0)                              { scpi.pulse_period[n] = value; update = 1; }
            else                                          { send_eps(EPA_REPLY_INVALID_ARG);          }
        }
        else if (equal(rest_p, ":CYCLES?", ":CYC?"))      { send_num(scpi.pulse_cycles[n]);           }
        else if (start(rest_p, ":CYCLES ", ":CYC ", arg))
        {
            long value = parse_long(arg, -1);
            if (value >= 0)                               { scpi.pulse_cycles[n] = value; update = 1; }
            else                                          { send_eps(EPA_REPLY_INVALID_ARG);          }
        }
        else if (equal(rest_p, ":INVERT?", ":INV?"))      { send_num(scpi.pulse_invert[n]);           }
        else if (start(rest_p, ":INVERT ", ":INV ", arg))
        {
            if      (equal(arg, "1"))                     { scpi.pulse_invert[n] = 1; update = 1;     }
            else if (equal(arg, "0"))                     { scpi.pulse_invert[n] = 0; update = 1;     }
            else                                          { send_eps(EPA_REPLY_INVALID_ARG);          }
        }
        else if (equal(rest_p, ":VALID?", ":VAL?"))       { send_num(scpi_pulse_valid[n]);            }
        else if (start(rest_p, ":VALID ", ":VAL ", NULL)) { send_eps(EPA_REPLY_READONLY);             }
        else                                              { send_eps(EPA_REPLY_INVALID_CMD);          }

        if (update)
        {
            bool ok = update_pulse(n);
            update_trig_ready();

            if (ok) { send_str("OK");            }
            else    { send_eps(EPA_REPLY_CHECK); }
        }
    }
    else                                                  { send_eps(EPA_REPLY_INVALID_CMD);          }
}
