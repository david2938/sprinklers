#include <ArduinoOTA.h>

#define NO_GLOBAL_ARDUINOOTA

class OTA : public ArduinoOTAClass {
    const char* hostname;
   public:
    OTA(const char* hostname);
    void setup();
    void loop();
};