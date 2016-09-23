#define ZERO_OK  1  // default behavior when omitted
#define ZERO_NOK 0

bool parse_int(const char *str, long &dest, bool zero_ok)
{
    long value = atoi(str);
    if (value > 0 || (str[0] == '0' && zero_ok))  // value == 0 could come from a conversion error
    {
        dest = value;  // always greater than (or equal to zero, if zero_ok is set)
        return 1;
    }
    else { return 0; }
}

bool parse_float(const char *str, float &dest, bool zero_ok)
{
    float value = atof(str);
    if (value > 0.0 || (str[0] == '0' && zero_ok))  // value == 0.0 could come from a conversion error
    {
        dest = value;  // always greater than (or equal to zero, if zero_ok is set)
        return 1;
    }
    else { return 0; }
}

bool parse_int(const char *str, long &dest)    { return parse_int(str,   dest, ZERO_OK); }
bool parse_float(const char *str, float &dest) { return parse_float(str, dest, ZERO_OK); }

bool split(const char *str, const char sep, int *offset, const int len)
{
    offset[0] = 0;  // first substring always starts at beginning (zero-length substrings are allowed)
    int j = 1;
    for (int i = 0; str[i] != 0; i++)
    {
        if (str[i] == sep)
        {
            offset[j] = i + 1;
            j++;
            if (j == len) { return 1; }
        }
    }
    return 0;
}

bool unhex(const char c0, const char c1, byte &value)
{
    byte msf, lsf;

    if      (c0 >= '0' && c0 <= '9') { msf = c0 - '0';      }
    else if (c0 >= 'a' && c0 <= 'f') { msf = c0 - 'a' + 10; }
    else if (c0 >= 'A' && c0 <= 'F') { msf = c0 - 'A' + 10; }
    else { return 0; }

    if      (c1 >= '0' && c1 <= '9') { lsf = c1 - '0';      }
    else if (c1 >= 'a' && c1 <= 'f') { lsf = c1 - 'a' + 10; }
    else if (c1 >= 'A' && c1 <= 'F') { lsf = c1 - 'A' + 10; }
    else { return 0; }

    value = msf * 16 + lsf;
    return 1;
}

bool parse_mac(const char *str, byte *dest)
{
    int offset[6];
    if (!split(str, ':', offset, 6)) { return 0; }

    byte addr[6];
    for (int j = 0; j < 6; j++)
    {
        byte value;
        const char *str_p = str + offset[j];
        if      (str_p[0] == ':' || str_p[0] == 0) { return 0; }  // zero digits . . .
        else if (str_p[1] == ':' || str_p[1] == 0) { if (!unhex('0',      str_p[0], value)) { return 0; } }
        else if (str_p[2] == ':' || str_p[2] == 0) { if (!unhex(str_p[0], str_p[1], value)) { return 0; } }
        else { return 0; }  // too many digits . . .

        addr[j] = value;
    }

    for (int j = 0; j < 6; j++) { dest[j] = addr[j]; }
    return 1;
}

bool parse_ip(const char *str, byte *dest)
{
    int offset[4];
    if (!split(str, '.', offset, 4)) { return 0; }

    byte addr[4];
    for (int j = 0; j < 4; j++)
    {
        const char *str_p = str + offset[j];
        long value = atoi(str_p);
        if (value < 0 || value > 255 || (value == 0 && str_p[0] != '0')) { return 0; }
        addr[j] = value;
    }

    for (int j = 0; j < 4; j++) { dest[j] = addr[j]; }
    return 1;
}

bool equal(const char *str, const char *cmp)
{
    return strcasecmp(str, cmp) == 0;
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

bool equal(const char *str, const char *cmp1, const char *cmp2)             { return equal(str, cmp1)       || equal(str, cmp2);       }
bool start(const char *str, const char *sub1, const char *sub2, char *rest) { return start(str, sub1, rest) || start(str, sub2, rest); }

