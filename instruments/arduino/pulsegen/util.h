// receiving/parsing helper functions:

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

long parse_long(const char *str, const long alt)
{
    long value = atoi(str);
    return (value != 0 || str[0] == '0') ? value : alt;  // value == 0 could come from a conversion error
}

float parse_float(const char *str, const float alt)
{
    float value = atof(str);
    return (value != 0.0 || str[0] == '0') ? value : alt;  // value == 0.0 could come from a conversion error
}

// replying helper functions:

void send_str(const char *str, const bool eol)
{
    if (eol) { Serial.println(str); }
    else     { Serial.print(str);   }
}

void send_eps(const int epa, const bool eol)
{
    char eps[ESLEN];
    EEPROM.get(epa, eps);

    if (eol) { Serial.println(eps); }
    else     { Serial.print(eps);   }
}

void send_hex(const unsigned long value, const bool eol)
{
    if (eol) { Serial.println(value, HEX); }
    else     { Serial.print(value, HEX); }
}

void send_num(const bool value, const bool eol)
{
    if (eol) { Serial.println(value); }
    else     { Serial.print(value);   }
}

void send_num(const long value, const bool eol)
{
    if (eol) { Serial.println(value); }
    else     { Serial.print(value);   }
}

void send_num(const float value, const bool eol)
{
    int digits = (value < 1e3) ? 6 : 1;

    if (eol) { Serial.println(value, digits); }
    else     { Serial.print(value, digits); }
}

#define EOL   1  // default behavior when omitted
#define NOEOL 0

void send_str(const char *str)           { send_str(str,   EOL); }
void send_eps(const int epa)             { send_eps(epa,   EOL); }
void send_hex(const unsigned long value) { send_hex(value, EOL); }
void send_num(const bool value)          { send_num(value, EOL); }
void send_num(const long value)          { send_num(value, EOL); }
void send_num(const float value)         { send_num(value, EOL); }
