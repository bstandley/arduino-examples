// description: test using an external clock to generate accurate delays

// notes:
//  - tested with 1MHz clock and 50kHz DAQ card, measured jitter:
//    t_low = 0.01s, k_up = 10000,   jitter ~ 20µs
//    t_low = 0.04s, k_up = 40000,   jitter ~ 20µs
//    t_low = 0.10s, k_up = 100000,  jitter ~ 20µs
//    t_low = 0.40s, k_up = 400000,  jitter ~ 120µs
//    t_low = 1.00s, k_up = 1000000, jitter ~ 300µs
//  - jitter could from clock source drift vs. DAQ card
//  - eventually needs to be tested with a proper (>10MHz) scope
//  - need to clarify whether accesses to TCNT1 are safe as written,
//    given that it continuously updates

// docs:
//  http://sphinx.mythic-beasts.com/~markt/ATmega-timers.html
//  http://maxembedded.com/2011/06/avr-timers-timer1/
//  http://www.pjrc.com/teensy/td_libs_FreqCount.html

const int    clock_pin  = 12;    // board-dependent, see docs
const int    input_pin  = 3;     // arbitrary
const int    output_pin = 2;     // arbitrary
const double clock_freq = 1e6;   // Hz
const double t_low      = 1.00;  // sec
const double t_high     = 0.01;  // sec

const unsigned long k_up = clock_freq * t_low;
const unsigned long k_dn = clock_freq * (t_low + t_high);

void run_sequence()
{
    unsigned int  c = TCNT1;
    unsigned long k = 0;
    bool          x = 0;

    while (1)
    {
        unsigned int c_diff = TCNT1 - c;

        c += c_diff;
        k += c_diff;

        if (!x && k >= k_up)
        {
            digitalWrite(output_pin, HIGH);
            x = 1;
        }
        else if (x && k >= k_dn)
        {
            digitalWrite(output_pin, LOW);
            break;
        }
    }
}

void setup()
{
    pinMode(clock_pin, INPUT);
    TCCR1A = 0x0;  // COM1A1=0 COM1A0=0 COM1B1=0 COM1B0=0 FOC1A=0 FOC1B=0 WMG11=0 WGM10=0 (see docs)
    TCCR1B = 0x7;  // ICNC1=0  ICES1=0  n/a=0    WGM13=0  WGM12=0 CS12=1  CS11=1  CS10=1  (see docs)

    pinMode(input_pin,  INPUT);
    pinMode(output_pin, OUTPUT);
    digitalWrite(output_pin, LOW);

    delay(2000);

    Serial.begin(9600);
    Serial.print("k_up = ");
    Serial.println(k_up);
    Serial.print("k_dn = ");
    Serial.println(k_dn);

    attachInterrupt(digitalPinToInterrupt(input_pin), run_sequence, RISING);
}

void loop()
{
    delay(1);
}
