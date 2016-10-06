#define ZERO_OK  1
#define ZERO_NOK 0

long pow10(const long x, const int z)
{
    long y = 1;
    for (int i = 0; i < abs(z); i++) { y *= 10; }
    return (z < 0) ? x / y : x * y;
}

bool parse_num(const char *str, long &value, const bool zero_ok, const int exp)
{
    int len = strlen(str);
    int dpt = -1;  // first occurance of decimal point
    int ept = -1;  // first occurance of exponent symbol
    for (int i = 0; i < len; i++)
    {
        if (dpt == -1 && str[i] == '.')                    { dpt = i; }
        if (ept == -1 && (str[i] == 'e' || str[i] == 'E')) { ept = i; }
    }

    long a = atol(str);
    if (a < 0 || (a == 0 && str[0] != '0' && str[0] != '.')) { return 0; }  // a == 0 could come from a conversion error

    long b = 0;
    if (dpt != -1)
    {
        const char *str_p = str + dpt + 1;
        b = atol(str_p);
        if (b < 0 || (b == 0 && str_p[0] != '0')) { return 0; }  // b == 0 could come from a conversion error
    }

    int c = 0;
    if (ept != -1)
    {
        const char *str_p = str + ept + 1;
        c = atoi(str_p);
        if (c == 0 && str_p[0] != '0') { return 0; }  // c == 0 could come from a conversion error
    }

    if (a > 0 || b > 0 || zero_ok)  // non-negativity already checked above
    {
        int dig = (dpt == -1) ? 0 : ((ept == -1) ? len - dpt - 1 : ept - dpt - 1);
        value = pow10(a, c + exp) + pow10(b, c + exp - dig);  // always greater than (or equal to zero, if zero_ok is set)
        return 1;
    }
    else { return 0; }
}

bool parse_num(const char *str, long &value, bool zero_ok)    { return parse_num(str, value, zero_ok, 0); }
bool parse_micros(const char *str, long &value, bool zero_ok) { return parse_num(str, value, zero_ok, 6); }

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
        byte tmp;
        const char *str_p = str + offset[j];
        if      (str_p[0] == ':' || str_p[0] == 0) { return 0; }  // zero digits . . .
        else if (str_p[1] == ':' || str_p[1] == 0) { if (!unhex('0',      str_p[0], tmp)) { return 0; } }
        else if (str_p[2] == ':' || str_p[2] == 0) { if (!unhex(str_p[0], str_p[1], tmp)) { return 0; } }
        else { return 0; }  // too many digits . . .

        addr[j] = tmp;
    }

    for (int j = 0; j < 6; j++) { dest[j] = addr[j]; }
    return 1;
}

bool parse_ip(const char *str, uint32_t &value)
{
    int offset[4];
    if (!split(str, '.', offset, 4)) { return 0; }

    uint32_t addr = 0;
    for (int j = 0; j < 4; j++)
    {
        const char *str_p = str + offset[j];
        long tmp = atoi(str_p);
        if (tmp < 0 || tmp > 255 || (tmp == 0 && str_p[0] != '0')) { return 0; }
        addr |= tmp << (j*8);  // & 0xFF not necessary due to above line
    }

    value = addr;
    return 1;
}

bool start_match(const char *str_p, char *rest)
{
    if (rest != NULL)
    {
        if (strlen(str_p) > 0) { strcpy(rest, str_p); }
        else                   { rest[0] = 0;         }

        return 1;  // start matches and remainder is allowed
    }
    else { return (str_p[0] == 0); }  // exact match required
}

bool start(const char *str, const char *pre, const char *opt, const char *suf, char *rest)
{
    int pre_len = strlen(pre);
    int opt_len = strlen(opt);
    int suf_len = strlen(suf);

    if (strncasecmp(str, pre, pre_len) == 0)
    {
        if      (strncasecmp(str + pre_len, opt, opt_len) == 0 && strncasecmp(str + pre_len + opt_len, suf, suf_len) == 0) { return start_match(str + pre_len + opt_len + suf_len, rest); }
        else if (strncasecmp(str + pre_len, suf, suf_len) == 0)                                                            { return start_match(str + pre_len + suf_len,           rest); }
    }

    return 0;
}

bool start(const char *str, const char *cmp, char *rest)                       { return start(str, cmp, "",  "",  rest); }
bool start(const char *str, const char *pre, const char *opt, char *rest)      { return start(str, pre, opt, "",  rest); }
bool equal(const char *str, const char *cmp)                                   { return start(str, cmp, "",  "",  NULL); }
bool equal(const char *str, const char *pre, const char *opt)                  { return start(str, pre, opt, "",  NULL); }
bool equal(const char *str, const char *pre, const char *opt, const char *suf) { return start(str, pre, opt, suf, NULL); }
