// description: test using a hardware timer/counter to count external pulses

// notes:
//  - tested on a Leonardo with a wire connecting pin 8 and 12 together
//  - 500 reps took about 3500 us, so the counter can read external signals up to at least 140 kHz
//  - max counting frequency is unclear (without actually consulting the datasheet) -- internal clock runs
//    at 16MHz but is divided down for use by the counters
//  - narrow pulses, i.e. sub-us, have not yet been tested
//  - overflow-handing code would be needed in a real application . . .

// docs:
//  http://sphinx.mythic-beasts.com/~markt/ATmega-timers.html
//  http://maxembedded.com/2011/06/avr-timers-timer1/
//  http://www.pjrc.com/teensy/td_libs_FreqCount.html

const int clock_input_pin = 12;  // board-dependent, see docs
const int test_signal_pin = 8;   // arbitrary
const int test_reps = 500;

void setup()
{
    pinMode(clock_input_pin, INPUT);
    TCCR1A = 0x0;  // COM1A1=0 COM1A0=0 COM1B1=0 COM1B0=0 FOC1A=0 FOC1B=0 WMG11=0 WGM10=0 (see docs)
    TCCR1B = 0x7;  // ICNC1=0  ICES1=0  n/a=0    WGM13=0  WGM12=0 CS12=1  CS11=1  CS10=1  (see docs)
    TCNT1  = 0;    // initial value

    pinMode(test_signal_pin, OUTPUT);
    digitalWrite(test_signal_pin, LOW);

    Serial.begin(9600);
}

void loop()
{
    unsigned int count = TCNT1;

    Serial.print("count = ");
    Serial.println(count);

    unsigned long t_start = micros();
    for (int i = 0; i < test_reps; i++)
    {
        digitalWrite(test_signal_pin, HIGH);
        digitalWrite(test_signal_pin, LOW);
    }
    unsigned long t_end = micros();

    Serial.print("generate ");
    Serial.print(test_reps);
    Serial.print(" test pulses in ");
    Serial.print(t_end - t_start);
    Serial.println(" us");

    delay(1000);
}
