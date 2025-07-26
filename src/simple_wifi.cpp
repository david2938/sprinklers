#include <simple_wifi.h>

SimpleWiFi::SimpleWiFi(const char* ssid, const char* passphrase) : 
    ssid(ssid), passphrase(passphrase) {}

void SimpleWiFi::setup() {
    Serial.println("Connecting to WiFi...");

    WiFi.begin(ssid, passphrase);

    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
    }

    Serial.print("Station connected, IP: ");
    Serial.println(WiFi.localIP());
}