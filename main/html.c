


#include "stdint.h"

#include "html.h"

#ifdef __cplusplus
extern "C" {
#endif

const char* html[4] = {
/*index*/    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><title>Index</title></head><body><a href=\"/\"> Index </a><br><a href=\"/wifi_config\">WiFi Config</a><br><a href=\"/mqtt_config\">MQTT Config</a></body></html>",
/*wifi_config*/    "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"UTF-8\"><title>WIFI Config</title><style type=\"text/css\">.form{text-align: center;}.button{text-align: center;font-size: 24px;height: 50px;width: 128px;}</style><script>function validateForm(){var ssid = document.forms[\"name_form\"][\"wifi_ssid\"].value;if(ssid==null || ssid==\"\"){alert(\"WiFi SSID is Empty\");return false;}}</script></head><body><form class=\"form\" name=\"name_form\" action=\"/wifi_save\" onsubmit=\"return validateForm()\" method=\"post\"><fieldset><legend class=\"legend\">WIFI Config</legend>WIFI SSID: <input class=\"input\" type=\"text\" name=\"wifi_ssid\" value=\"\"><br>WIFI PASSWD: <input class=\"input\" type=\"password\" name=\"wifi_passwd\" value=\"\"><br></fieldset><button class=\"button\" type=\"submit\" name=\"save\" value=\"Save\">Save</button></form></body></html>",
/*mqtt_config*/    "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"UTF-8\"><title>MQTT Config</title><style type=\"text/css\">.form{text-align: center;}.button{text-align:center;font-size:24px;height:50px;width:128px;}</style><script>function validateForm() {var mqtt_host = document.forms[\"name_form\"][\"mqtt_host\"].value;if(mqtt_host==null || mqtt_host==\"\"){alert(\"MQTT Broker Host is Empty\");return false;}}</script></head><body><form class=\"form\" name=\"name_form\" action=\"/mqtt_save\" onsubmit=\"return validateForm()\" method=\"post\"><fieldset><legend class=\"legend\">MQTT Broker Config</legend>MQTT Broker Host: <input class=\"input\" type=\"text\" name=\"mqtt_host\" value=\"\"><br>MQTT Broker Port: <input class=\"input\" type=\"number\" name=\"mqtt_port\" value=\"\"><br>MQTT Broker Client ID: <input class=\"input\" type=\"text\" name=\"mqtt_client_id\" value=\"\"><br>MQTT Broker Username: <input class=\"input\" type=\"text\" name=\"mqtt_username\" value=\"\"><br>MQTT Broker Password: <input class=\"input\" type=\"password\" name=\"mqtt_passwd\" value=\"\"><br></fieldset><button class=\"button\" type=\"submit\" name=\"save\" value=\"Save\">Save</button></form></body></html>",
/*save success*/   "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><title>Save</title></head><body><script>alert(\"Save Successed!\");</script></body></html>",
/*save failure*/   "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><title>Save</title></head><body><script>alert(\"Save Failure!\");</script></body></html>",
};

#ifdef __cplusplus
}
#endif

