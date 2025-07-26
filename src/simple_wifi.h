#include <ESP8266WiFi.h>

class SimpleWiFi {
    const char* ssid;
    const char* passphrase;

   public:
    SimpleWiFi(const char* ssid, const char* passphrase);
    void setup();

    // for now, the SimpleWiFi class has no need for a loop() function
    // void loop();
};