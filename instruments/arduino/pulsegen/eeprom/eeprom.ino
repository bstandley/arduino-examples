#include "shared.h"

const unsigned long conf_commit                    = 0x1234abc;  // edit to match current commit before compile/download!
const char          conf_idn               [ESLEN] = "SDI PULSE GENERATOR";
const char          conf_reply_check       [ESLEN] = "WARNING: CHECK CHANNEL TIMING";
const char          conf_reply_readonly    [ESLEN] = "ERROR: READ-ONLY SETTING";
const char          conf_reply_invalid_cmd [ESLEN] = "ERROR: INVALID COMMAND OR QUERY";
const char          conf_reply_invalid_arg [ESLEN] = "ERROR: INVALID ARGUMENT";
const char          conf_reply_reboot      [ESLEN] = "INFO: REBOOT TO APPLY LAN SETTINGS";

void setup()
{
    EEPROM.put(EPA_COMMIT, conf_commit);

    SCPI scpi;
    scpi_default(scpi);
    EEPROM.put(EPA_SCPI, scpi);

    SCPI_LAN scpi_lan;
    scpi_lan_initial(scpi_lan);
    EEPROM.put(EPA_SCPI_LAN, scpi_lan);

    EEPROM.put(EPA_IDN,               conf_idn);
    EEPROM.put(EPA_REPLY_CHECK,       conf_reply_check);
    EEPROM.put(EPA_REPLY_READONLY,    conf_reply_readonly);
    EEPROM.put(EPA_REPLY_INVALID_CMD, conf_reply_invalid_cmd);
    EEPROM.put(EPA_REPLY_INVALID_ARG, conf_reply_invalid_arg);
    EEPROM.put(EPA_REPLY_REBOOT,      conf_reply_reboot);

    Serial.begin(9600);
}

void loop()
{
    unsigned long commit;
    EEPROM.get(EPA_COMMIT, commit);

    Serial.print("EPA_COMMIT/");
    Serial.print(EPA_COMMIT);
    Serial.print("/");
    Serial.print(sizeof(commit));
    Serial.print("/");
    Serial.println(commit, HEX);

    SCPI scpi;
    EEPROM.get(EPA_SCPI, scpi);

    Serial.print("EPA_SCPI/");
    Serial.print(EPA_SCPI);
    Serial.print("/");
    Serial.print(sizeof(scpi));
    Serial.println("/...");

    SCPI_LAN scpi_lan;
    EEPROM.get(EPA_SCPI_LAN, scpi_lan);

    Serial.print("EPA_SCPI_LAN/");
    Serial.print(EPA_SCPI_LAN);
    Serial.print("/");
    Serial.print(sizeof(scpi_lan));
    Serial.println("/...");

    check_eps(EPA_IDN,               "EPA_IDN");
    check_eps(EPA_REPLY_CHECK,       "EPA_CHECK");
    check_eps(EPA_REPLY_READONLY,    "EPA_REPLY_READONLY");
    check_eps(EPA_REPLY_INVALID_CMD, "EPA_REPLY_INVALID_CMD");
    check_eps(EPA_REPLY_INVALID_ARG, "EPA_REPLY_INVALID_ARG");
    check_eps(EPA_REPLY_REBOOT,      "EPA_REPLY_REBOOT");

    delay(4000);
}

void check_eps(const int epa, const char *name)
{
    char eps[ESLEN];
    EEPROM.get(epa, eps);

    Serial.print(name);
    Serial.print("/");
    Serial.print(epa);
    Serial.print("/");
    Serial.print(sizeof(eps));
    Serial.print("/");
    Serial.println(eps);
}

void scpi_lan_initial(SCPI_LAN &s)
{
    s.mode = LAN_DHCP;
    s.mac[0]            = 0x6;
    s.mac[1]            = 0x5;
    s.mac[2]            = 0x4;
    s.mac[3]            = 0x3;
    s.mac[4]            = 0x2;
    s.mac[5]            = 0x1;
    s.ip_static[0]      = 192;
    s.ip_static[1]      = 168;
    s.ip_static[2]      = 0;
    s.ip_static[3]      = 100;
    s.gateway_static[0] = 192;
    s.gateway_static[1] = 168;
    s.gateway_static[2] = 0;
    s.gateway_static[3] = 1;
    s.subnet_static[0]  = 255;
    s.subnet_static[1]  = 255;
    s.subnet_static[2]  = 255;
    s.subnet_static[3]  = 0;
}
