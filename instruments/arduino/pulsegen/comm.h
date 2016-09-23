bool recv_msg(char *msg)
{
    for (int i = 0; i < MSGLEN; i++)
    {
        int b = Serial.read();
        if (b == -1 || b == '\n' || b == '\r' || i == MSGLEN - 1)
        {
            msg[i] = 0;
            break;
        }
        else { msg[i] = b; }
    }
    while (Serial.read() != -1);

    return (msg[0] != 0);
}

void send_str(const char *str, const bool eol)
{
    if (eol) { Serial.println(str); }
    else     { Serial.print(str);   }
}

void send_eps(const int epa, const bool eol)
{
    char eps[ESLEN];
    EEPROM.get(epa, eps);
    send_str(eps, eol);
}

void send_num(const long value, const bool eol, const byte base)
{
    if (eol) { Serial.println(value, base); }
    else     { Serial.print(value,   base); }
}

void send_int(const long value, const bool eol) { send_num(value, eol, DEC); }
void send_hex(const long value, const bool eol) { send_num(value, eol, HEX); }

void send_float(const float value, const bool eol)
{
    int digits = (value < 1e3) ? 6 : 1;

    if (eol) { Serial.println(value, digits); }
    else     { Serial.print(value,   digits); }
}

#define EOL   1  // default behavior when omitted
#define NOEOL 0

void send_str(const char *str)     { send_str(str,     EOL); }
void send_eps(const int epa)       { send_eps(epa,     EOL); }
void send_int(const long value)    { send_int(value,   EOL); }
void send_hex(const long value)    { send_hex(value,   EOL); }
void send_float(const float value) { send_float(value, EOL); }
