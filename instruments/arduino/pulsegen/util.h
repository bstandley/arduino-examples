
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

long parse_int(const char *str, const long alt)
{
    String string = String(str);
    long value = string.toInt();
    return (value != 0 || str[0] == '0') ? value : alt;  // value == 0 could come from a conversion error
}

float parse_float(const char *str, const float alt)
{
    String string = String(str);
    float value = string.toFloat();
    return (value != 0.0 || str[0] == '0') ? value : alt;  // value == 0.0 could come from a conversion error
}
