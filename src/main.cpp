/*
 * Copyright 2025 David Main
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <ShiftRegister74HC595.h>
#include <LittleFS.h>
#include <NTPClient.h>
#include <ota.h>
#include <simple_wifi.h>
#include <secrets.h>
#include <SprinklerAPI.hpp>
#include <Ticker.h>

#define MTN_DAYLIGHT_OFFSET_SECONDS (long)(-6 * 60 * 60)
#define MTN_STANDARD_OFFSET_SECONDS (long)(-7 * 60 * 60)

// ssid_name, ssid_password come from secrets.h
SimpleWiFi wifi(ssid_name, ssid_password);
OTA ota(DEVICE_NAME);
ESP8266WebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, MTN_DAYLIGHT_OFFSET_SECONDS);
ShiftRegister74HC595<1> shiftRegister(
    /* serialDataPin */ D5, 
    /* clockPin      */ D8, 
    /* latchPin      */ D7
);
SprinklerAPI api(server, shiftRegister, timeClient, 7, D0);
unsigned long now = millis();
unsigned long newMillis = 0L;
Ticker heartbeat[2];

void setup() {
    unsigned long setupStart = millis();
    unsigned long setupEnd = 0L;

// do this immediately to ensure start up has no zones on
#ifdef NORMAL_LOGIC
    api.setNormalLogic(true);
    shiftRegister.setAllLow();
#else
    api.setNormalLogic(false);
    shiftRegister.setAllHigh();
#endif

    Serial.begin(115200);

    if (!LittleFS.begin()) {
        api.setFsAvailable(false);
    }

    wifi.setup();
    ota.setup();

    // because SprinklerAPI uses timeClient, it must be set up first, and the
    // update is required to apply the timezone offset from the constructor
    timeClient.begin();
    timeClient.update();

    api.setup();

    api.blinkLed(3, 300, 150);

    // make the heartbeat LED blink every 5 seconds for a very short blip
    // of 50 milliseconds
    heartbeat[0].attach(5, []() {
        digitalWrite(LED_BUILTIN, LOW);

        // turn the LED off 50 ms later
        heartbeat[1].once_ms(50, []() {
            digitalWrite(LED_BUILTIN, HIGH);
        });
    });

    setupEnd = millis();

    api.logMsgf("setup duration=%lu", setupEnd - setupStart);
}

void loop() {
    timeClient.update();
    
    newMillis = millis();

    if (newMillis < now) {
        // When newMillis is not greater than now, this is kind of a problem.
        // First, we need to capture that this happened, and then we need to 
        // manually advance now by just 1 millisecond so that for the purposes
        // of the controller, it thinks time has moved slowly forward.  It is
        // expected that the next time around, millis() will yield a much
        // larger value.  It is anticipated that this is only a seldonly
        // occurring situation, but I want to know about it.

        api.logMsgf("error - newMillis=%lu < now=%lu", newMillis, now);
    } else {
        now = newMillis;
    }

    ota.loop();
    api.loop();
}