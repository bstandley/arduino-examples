#define NCHAN 4
#define MSGLEN 64  // includes null-terminator
#define MEAS_MS 500
#define START_US 10

enum reply_t {REPLY_NONE, REPLY_OK, REPLY_UPDATE, REPLY_CHECK, REPLY_READONLY, REPLY_WRITEONLY, REPLY_INVALID_CMD, REPLY_INVALID_ARG};

const byte clock_pin         = 12;                                // board-dependent
const byte trig_pin          = 2;                                 // pin must support low-level interrupts
const byte pulse_pin[NCHAN]  = {8, 7, 6, 5};
byte *     pulse_port[NCHAN] = {&PORTB, &PORTE, &PORTD, &PORTC};  // board-dependent, must match pulse_pin
const byte pulse_mask[NCHAN] = {1 << 4, 1 << 6, 1 << 7, 1 << 6};  // board dependent, must match pulse_pin

long k_delay[NCHAN];
long k_width[NCHAN];
long k_period[NCHAN];
long k_end[NCHAN];

volatile int          N_active;
volatile long         k_next[NCHAN];
volatile bool         x_next[NCHAN];
volatile long         k_cur;
volatile unsigned int c_cur;

// notes on SCPI-style commands:
//   - abbreviations are supported where noted, e.g WIDth matches both WID and WIDTH
//   - bool values must be 0 or 1
//   - <n> in pulse configs is {1, 2, 3, 4} for outputs {A, B, C, D}
//   - if WIDTH > PERIOD, the pulse is continuous, i.e. always high if not inverted, full sequence will last DELAY + CYCLES*PERIOD
//   - (DELAY + PERIOD*CYCLES)/FREQ must be < 2e9, otherwise the channel will not be used (VALID = 0)

const char    var_idn[]               = "SDI pulse generator, 20160919";  // *IDN                  (r)   -   model and version
                                                                          // *TRG                  (w)   -   soft trigger, independent of :TRIG:ARMED
byte          var_clock_src           = INTERNAL;                         // :CLOCK:SRC            (rw)  -   INTernal, EXTernal
float         var_clock_freq;                                             // :CLOCK:FREQ           (r)   Hz  ideal current frequency
                                                                          // :CLOCK:FREQ:MEASure   (r)   Hz  current frequency, measured against internal clock
float         var_clock_freq_int      = 2e6;                              // :CLOCK:FREQ:INTernal  (r)   Hz  ideal internal frequency -- board-dependent, assumes prescaler set to /8
float         var_clock_freq_ext      = 1e6;                              // :CLOCK:FREQ:EXTernal  (rw)  Hz  ideal external frequency -- max 5e6
byte          var_trig_edge           = RISING;                           // :TRIG:EDGE            (rw)  -   RISing, FALLing
volatile bool var_trig_armed          = 1;                                // :TRIG:ARMed           (rw)  -   armed
volatile bool var_trig_ready;                                             // :TRIG:READY           (r)   -   ready (armed plus at least one valid channel)
bool          var_trig_rearm          = 1;                                // :TRIG:REARM           (rw)  -   rearm after pulse sequence
volatile long var_trig_count          = 0;                                // :TRIG:COUNT           (r)   -   hardware triggers detected since reboot
float         var_pulse_delay[NCHAN]  = {0.0,  0.0,  0.0,  0.0};          // :PULSe<n>:DELay       (rw)  s   delay to first pulse
float         var_pulse_width[NCHAN]  = {0.01, 0.01, 0.01, 0.01};         // :PULSe<n>:WIDth       (rw)  s   pulse width
float         var_pulse_period[NCHAN] = {0.02, 0.02, 0.02, 0.02};         // :PULSe<n>:PERiod      (rw)  s   pulse period
long          var_pulse_cycles[NCHAN] = {1, 0, 0, 0};                     // :PULSe<n>:CYCles      (rw)  -   number of pulses
bool          var_pulse_invert[NCHAN] = {0, 0, 0, 0};                     // :PULSe<n>:INVert      (rw)  -   0 = non-inverting, 1 = inverting
bool          var_pulse_valid[NCHAN];                                     // :PULSe<n>:VALid       (r)   -   output channel has valid/usable pulse sequence

void setup()
{
    pinMode(clock_pin, INPUT);
    update_clock();

    for (int i = 0; i < NCHAN; i++)
    {
        pinMode(pulse_pin[i], OUTPUT);
        update_pulse(i);
    }

    pinMode(trig_pin, INPUT);
    update_trig_ready();
    update_trig_edge();  // actually configure interrupt

    Serial.begin(9600);
}

void loop()
{
    if (Serial.available() > 0)
    {
        char msg[MSGLEN];
        int n = Serial.readBytesUntil('\n', msg, MSGLEN - 1);
        while (Serial.read() != -1);

        if (n > 0)
        {
            msg[n] = 0;  // NULL termination
            if (msg[n-1] == '\r') { msg[n-1] = 0; }  // TODO: test this feature
            parse_msg(msg);
        }
    }

    delay(1);
}

bool update_clock()
{
    var_clock_freq = (var_clock_src == INTERNAL) ? var_clock_freq_int : var_clock_freq_ext;

    TCCR1A = 0x0;                                 // COM1A1=0 COM1A0=0 COM1B1=0 COM1B0=0 FOC1A=0 FOC1B=0 WMG11=0 WGM10=0
    TCCR1B = (var_clock_src == INTERNAL) ? 0x2 :  // ICNC1=0  ICES1=0  n/a=0    WGM13=0  WGM12=0 CS12=0  CS11=1  CS10=0  (internal, /8)
                                           0x7;   // ICNC1=0  ICES1=0  n/a=0    WGM13=0  WGM12=0 CS12=1  CS11=1  CS10=1  (external rising)
}

void update_trig_edge()
{
    attachInterrupt(digitalPinToInterrupt(trig_pin), run_null, var_trig_edge);
    delay(100);  // let possibly lingering interrupt clear out
    attachInterrupt(digitalPinToInterrupt(trig_pin), run_hw_trig, var_trig_edge);
}

bool update_pulse(int i)
{
    digitalWrite(pulse_pin[i], var_pulse_invert[i]);  // if inverting, then set initial value HIGH	

    k_delay[i]  = var_clock_freq * var_pulse_delay[i];
    k_width[i]  = var_clock_freq * min(var_pulse_width[i], var_pulse_period[i]);
    k_period[i] = var_clock_freq * var_pulse_period[i];
    k_end[i]    = k_delay[i] + k_period[i] * var_pulse_cycles[i];

    return var_pulse_valid[i] = (var_clock_freq * (var_pulse_delay[i] + var_pulse_period[i] * var_pulse_cycles[i]) < 2e9);  // calculate with floats
}

void update_trig_ready()
{
    k_cur = var_clock_freq * START_US / 1e6;  // account for interrupt processing time
    N_active = 0;

    for (int i = 0; i < NCHAN; i++)
    {
        if (var_pulse_valid[i] && (var_pulse_cycles[i] > 0))
        {
            N_active++;
            k_next[i] = k_delay[i];
            x_next[i] = 1;
        }
        else { k_next[i] = 2100000000; }
    }

    var_trig_ready = var_trig_armed && N_active > 0;
}

void run_null() { return; }

void run_hw_trig()
{
    c_cur = TCNT1;       // TESTING: read as early as possible to make global timebase as accurate as possible
    if (var_trig_ready)  // TESTING: precalculated to save a bit of time
    {
        gen_pulses();
        var_trig_armed = var_trig_rearm;
        update_trig_ready();
    }
    var_trig_count++;
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
    for (int i = 0; i < NCHAN; i++)  // TESTING: quick iteration to accelerate short delays
    {
        if (k_cur >= k_next[i])  // use x_next[i] == 1 below:
        {
            pulse_write(i, !var_pulse_invert[i]);

            k_next[i] += k_width[i];
            x_next[i] = 0;
        }
    }
    
    var_trig_ready = 0;  // ok to set now  TODO: test against spurious triggers

    while (1)
    {
        unsigned int c_diff = TCNT1 - c_cur;
        c_cur += c_diff;
        k_cur += c_diff;

        for (int i = 0; i < NCHAN; i++)  // full loop
        {
            if (k_cur >= k_next[i])
            {
                pulse_write(i, x_next[i] ? !var_pulse_invert[i] : var_pulse_invert[i]);

                k_next[i] += x_next[i] ? k_width[i] : (k_period[i] - k_width[i]);
                x_next[i] = !x_next[i];

                if (k_next[i] >= k_end[i])
                {
                    pulse_write(i, var_pulse_invert[i]);
                    k_next[i] = 2100000000;
                    N_active--;
                    if (N_active == 0) { return; }
                }
            }
        }
    }
}

void pulse_write(int i, bool x)  // TESTING: a bit quicker than digitalWrite()
{
    if (x) { *pulse_port[i] |=  pulse_mask[i]; }
    else   { *pulse_port[i] &= ~pulse_mask[i]; }
}

float measure_freq()
{
    unsigned long t_start = millis();
    unsigned int  c = TCNT1;
    long          k = 0;

    while(millis() - t_start <= MEAS_MS)
    {
        unsigned int c_diff = TCNT1 - c;
        c += c_diff;
        k += c_diff;
    }

    return 1e3 * float(k) / float(MEAS_MS);
}

void parse_msg(const char *msg)
{
    reply_t reply = REPLY_INVALID_CMD;
    char rest[MSGLEN];

    if      (equal(msg, "*IDN?"))                 { reply = REPLY_NONE; Serial.println(var_idn); }
    else if (start(msg, "*IDN ", NULL))           { reply = REPLY_READONLY;                      }
    else if (equal(msg, "*TRG?"))                 { reply = REPLY_WRITEONLY;                     }
    else if (equal(msg, "*TRG"))                  { reply = REPLY_OK; run_sw_trig();             }
    else if (start(msg, ":CLOCK", rest))          { parse_clock(rest, &reply);                   }
    else if (start(msg, ":TRIG", rest))           { parse_trig(rest, &reply);                    }
    else if (start(msg, ":PULSE", ":PULS", rest)) { parse_pulse(rest, &reply);                   }

    if      (reply == REPLY_OK)          { Serial.println("OK");                              }
    else if (reply == REPLY_CHECK)       { Serial.println("WARNING: CHECK CHANNEL TIMING");   }
    else if (reply == REPLY_READONLY)    { Serial.println("ERROR: READ-ONLY SETTING");        }
    else if (reply == REPLY_WRITEONLY)   { Serial.println("ERROR: WRITE-ONLY SETTING");       }
    else if (reply == REPLY_INVALID_CMD) { Serial.println("ERROR: INVALID QUERY OR COMMAND"); }
    else if (reply == REPLY_INVALID_ARG) { Serial.println("ERROR: INVALID ARGUMENT");         }
}

void parse_clock(const char *rest, reply_t *reply)
{
    char arg[MSGLEN];

    if      (equal(rest, ":SRC?"))                                { *reply = REPLY_NONE; Serial.println(var_clock_src == INTERNAL ? "INTERNAL" : "EXTERNAL"); }
    else if (start(rest, ":SRC ", arg))
    {
        if      (equal(arg, "INTERNAL", "INT"))                   { *reply = REPLY_UPDATE; var_clock_src = INTERNAL;                                          }
        else if (equal(arg, "EXTERNAL", "EXT"))                   { *reply = REPLY_UPDATE; var_clock_src = EXTERNAL;                                          }
        else                                                      { *reply = REPLY_INVALID_ARG;                                                               }

    }
    else if (equal(rest, ":FREQ:MEASURE?",  ":FREQ:MEAS?"))       { *reply = REPLY_NONE; Serial.println(measure_freq(), 1);                                   }
    else if (start(rest, ":FREQ:MEASURE ",  ":FREQ:MEAS ", NULL)) { *reply = REPLY_READONLY;                                                                  }
    else if (equal(rest, ":FREQ:INTERNAL?", ":FREQ:INT?"))        { *reply = REPLY_NONE; Serial.println(var_clock_freq_int, 1);                               }
    else if (start(rest, ":FREQ:INTERNAL ", ":FREQ:INT ", NULL))  { *reply = REPLY_READONLY;                                                                  }
    else if (equal(rest, ":FREQ:EXTERNAL?", ":FREQ:EXT?"))        { *reply = REPLY_NONE; Serial.println(var_clock_freq_ext, 1);                               }
    else if (start(rest, ":FREQ:EXTERNAL ", ":FREQ:EXT ", arg))
    {
            float value = parse_float(arg, -1.0);
            if (value != -1.0)                                    { *reply = REPLY_UPDATE; var_clock_freq_ext = value;                                        }
            else                                                  { *reply = REPLY_INVALID_ARG;                                                               }
    }
    else if (equal(rest, ":FREQ?"))                               { *reply = REPLY_NONE; Serial.println(var_clock_freq, 1);                                   }
    else if (start(rest, ":FREQ ", NULL))                         { *reply = REPLY_READONLY;                                                                  }

    if (*reply == REPLY_UPDATE)
    {
        bool ok = 1;
        update_clock();
        for (int i = 0; i < NCHAN; i++) { if (!update_pulse(i)) { ok = 0; } }  // at least one channel might be not ok . . .
        update_trig_ready();
        *reply = ok ? REPLY_OK : REPLY_CHECK;
    }
}

void parse_trig(const char *rest, reply_t *reply)
{
    char arg[MSGLEN];

    if      (equal(rest, ":EDGE?"))                { *reply = REPLY_NONE; Serial.println(var_trig_edge == RISING ? "RISING" : "FALLING"); }
    else if (start(rest, ":EDGE ", arg))
    {
        if      (equal(arg, "RISING",  "RIS"))     { *reply = REPLY_UPDATE; var_trig_edge = RISING;                                       }
        else if (equal(arg, "FALLING", "FALL"))    { *reply = REPLY_UPDATE; var_trig_edge = FALLING;                                      }
        else                                       { *reply = REPLY_INVALID_ARG;                                                          }
    }
    else if (equal(rest, ":ARMED?", ":ARM?"))      { *reply = REPLY_NONE; Serial.println(var_trig_armed);                                 }
    else if (start(rest, ":ARMED ", ":ARM ", arg))
    {
        if      (equal(arg, "1"))                  { *reply = REPLY_UPDATE; var_trig_armed = 1;                                           }
        else if (equal(arg, "0"))                  { *reply = REPLY_UPDATE; var_trig_armed = 0;                                           }
        else                                       { *reply = REPLY_INVALID_ARG;                                                          }

    }
    else if (equal(rest, ":READY?"))               { *reply = REPLY_NONE; Serial.println(var_trig_ready);                                 }
    else if (start(rest, ":READY ", NULL))         { *reply = REPLY_READONLY;                                                             }
    else if (equal(rest, ":REARM?"))               { *reply = REPLY_NONE; Serial.println(var_trig_rearm);                                 }
    else if (start(rest, ":REARM ", arg))
    {
        if      (equal(arg, "1"))                  { *reply = REPLY_OK; var_trig_rearm = 1;                                               }
        else if (equal(arg, "0"))                  { *reply = REPLY_OK; var_trig_rearm = 0;                                               }
        else                                       { *reply = REPLY_INVALID_ARG;                                                          }
    }
    else if (equal(rest, ":COUNT?"))               { *reply = REPLY_NONE; Serial.println(var_trig_count);                                 }
    else if (start(rest, ":COUNT ", NULL))         { *reply = REPLY_READONLY;                                                             }

    if (*reply == REPLY_UPDATE)
    {
        update_trig_ready();  // always ok
        update_trig_edge();   // always ok
        *reply = REPLY_OK;
    }
}

void parse_pulse(const char *rest, reply_t *reply)
{
    char arg[MSGLEN];

    int i = (rest[0] == '1') ? 0 :
            (rest[0] == '2') ? 1 :
            (rest[0] == '3') ? 2 :
            (rest[0] == '4') ? 3 :
                               -1;  // also handles case where msg == ":PULSE" (or ":PULS"), where rest[0] == 0

    if (i != -1)
    {
        char *rest_p = rest + 1;  // safe because we know strlen(rest) > 0
        if      (equal(rest_p, ":DELAY?", ":DEL?"))       { *reply = REPLY_NONE; Serial.println(var_pulse_delay[i], 6);  }
        else if (start(rest_p, ":DELAY ", ":DEL ", arg))
        {
            float value = parse_float(arg, -1.0);
            if (value >= 0.0 )                            { *reply = REPLY_UPDATE; var_pulse_delay[i] = value;           }
            else                                          { *reply = REPLY_INVALID_ARG;                                  }
        }
        else if (equal(rest_p, ":WIDTH?", ":WID?"))       { *reply = REPLY_NONE; Serial.println(var_pulse_width[i], 6);  }
        else if (start(rest_p, ":WIDTH ", ":WID ", arg))
        {
            float value = parse_float(arg, -1.0);
            if (value > 0.0 )                             { *reply = REPLY_UPDATE; var_pulse_width[i] = value;           }
            else                                          { *reply = REPLY_INVALID_ARG;                                  }
        }
        else if (equal(rest_p, ":PERIOD?", ":PER?"))      { *reply = REPLY_NONE; Serial.println(var_pulse_period[i], 6); }
        else if (start(rest_p, ":PERIOD ", ":PER ", arg))
        {
            float value = parse_float(arg, -1.0);
            if (value > 0.0 )                             { *reply = REPLY_UPDATE; var_pulse_period[i] = value;          }
            else                                          { *reply = REPLY_INVALID_ARG;                                  }
        }
        else if (equal(rest_p, ":CYCLES?", ":CYC?"))      { *reply = REPLY_NONE; Serial.println(var_pulse_cycles[i]);    }
        else if (start(rest_p, ":CYCLES ", ":CYC ", arg))
        {
            long value = parse_int(arg, -1.0);
            if (value >= 0 )                              { *reply = REPLY_UPDATE; var_pulse_cycles[i] = value;          }
            else                                          { *reply = REPLY_INVALID_ARG;                                  }
        }
        else if (equal(rest_p, ":INVERT?", ":INV?"))      { *reply = REPLY_NONE; Serial.println(var_pulse_invert[i]);    }
        else if (start(rest_p, ":INVERT ", ":INV ", arg))
        {
            if      (equal(arg, "1"))                     { *reply = REPLY_UPDATE; var_pulse_invert[i] = 1;              }
            else if (equal(arg, "0"))                     { *reply = REPLY_UPDATE; var_pulse_invert[i] = 0;              }
            else                                          { *reply = REPLY_INVALID_ARG;                                  }
        }
        else if (equal(rest_p, ":VALID?", ":VAL?"))       { *reply = REPLY_NONE; Serial.println(var_pulse_valid[i]);     }
        else if (start(rest_p, ":VALID ", ":VAL ", NULL)) { *reply = REPLY_READONLY;                                     }

        if (*reply == REPLY_UPDATE)
        {
            bool ok = update_pulse(i);
            update_trig_ready();
            *reply = ok ? REPLY_OK : REPLY_CHECK;
        }
    }
}

bool equal(const char *str, const char *cmp)
{
    return strcasecmp(str, cmp) == 0;
}

bool equal(const char *str, const char *cmp1, const char *cmp2)
{
    return equal(str, cmp1) || equal(str, cmp2);
}

bool start(const char *str, const char *sub, char *rest)
{
    int sub_len = strlen(sub);
    bool rv = (strncasecmp(sub, str, sub_len) == 0);

    if (rest != NULL)
    {
        if (rv && strlen(str) > sub_len) { strcpy(rest, str + sub_len); }
        else                             { rest[0] = 0; }
    }

    return rv;
}

bool start(const char *str, const char *sub1, const char *sub2, char *rest)
{
    return start(str, sub1, rest) ? 1 : start(str, sub2, rest);
}

long parse_int(const char *str, long alt)
{
    String string = String(str);
    long value = string.toInt();
    return (value != 0 || str[0] == '0') ? value : alt;  // value == 0 could come from a conversion error
}

float parse_float(const char *str, float alt)
{
    String string = String(str);
    float value = string.toFloat();
    return (value != 0.0 || str[0] == '0') ? value : alt;  // value == 0.0 could come from a conversion error
}
