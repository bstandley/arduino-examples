#ifdef LAN
#include <Ethernet.h>
EthernetClient client;
#endif

bool lan;  // set before calling communication functions!

int read_byte()
{
    return !lan ? Serial.read() :
#ifdef LAN
                  client.read();
#else
                  -1;  // just in case lan is set while LAN is undefined
#endif
}

bool recv_msg(char *msg)
{
    for (int i = 0; i < MSGLEN; i++)
    {
        int b = read_byte();
        if (b == -1 || b == '\n' || b == '\r' || i == MSGLEN - 1)
        {
            msg[i] = 0;
            break;
        }
        else { msg[i] = b; }
    }
    while (read_byte() != -1);

    return (msg[0] != 0);
}

void send_str(const char *str, const bool eol)
{
    if (!lan)
    {
        if (eol) { Serial.println(str); }
        else     { Serial.print(str);   }
    }
#ifdef LAN
    else
    {
        if (eol) { client.println(str); }
        else     { client.print(str);   }
    }
#endif
}

void send_eps(const int epa, const bool eol)
{
    char eps[ESLEN];
    EEPROM.get(epa, eps);
    send_str(eps, eol);
}

void send_num(const long value, const bool eol, const byte base)
{
    if (!lan)
    {
        if (eol) { Serial.println(value, base); }
        else     { Serial.print(value,   base); }
    }
#ifdef LAN
    else
    {
        if (eol) { client.println(value, base); }
        else     { client.print(value,   base); }
    }
#endif
}

void send_int(const long value, const bool eol) { send_num(value, eol, DEC); }
void send_hex(const long value, const bool eol) { send_num(value, eol, HEX); }

#define EOL   1  // default behavior when omitted
#define NOEOL 0

void send_micros(const long value, const bool eol)
{
    long a = value / 1000000;
    long b = value % 1000000;

    send_int(a,   NOEOL);
    send_str(".", NOEOL);
    if (b > 0)
    {
        for (long d = b; d < 100000; d *= 10) { send_str("0", NOEOL); }
        send_int(b, eol);
    }
    else { send_str("000000", eol); }
}

void send_mac(const byte *addr, const bool eol)
{
    for (int i = 0; i < 5; i++)
    {
        send_hex(addr[i], NOEOL);
        send_str(":",     NOEOL);
    }
    send_hex(addr[5], eol);
}

void send_ip(const byte *addr, const bool eol)
{
    for (int i = 0; i < 3; i++)
    {
        send_int(addr[i], NOEOL);
        send_str(".",     NOEOL);
    }
    send_int(addr[3], eol);
}

void send_str(const char *str)     { send_str(str,      EOL); }
void send_eps(const int epa)       { send_eps(epa,      EOL); }
void send_int(const long value)    { send_int(value,    EOL); }
void send_hex(const long value)    { send_hex(value,    EOL); }
void send_micros(const long value) { send_micros(value, EOL); }
void send_mac(const byte *addr)    { send_mac(addr,     EOL); }
void send_ip(const byte *addr)     { send_ip(addr,      EOL); }
