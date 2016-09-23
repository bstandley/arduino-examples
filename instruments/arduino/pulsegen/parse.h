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

