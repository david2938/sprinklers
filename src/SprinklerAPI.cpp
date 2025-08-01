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
#include <ESP8266WiFi.h>
#include <uri/UriBraces.h>
#include <SprinklerAPI.hpp>
#include <stdio.h>
#include <ctime>

#ifdef LOGGING_INFO
#define LOG_INFO(format, ...) \
    Serial.printf(format __VA_OPT__(,) __VA_ARGS__)
#else
#define LOG_INFO(...) do { (void)0; } while (0)
#endif

#ifdef LOGGING_DEBUG
#define LOG_DEBUG(format, ...) \
    Serial.printf(format __VA_OPT__(,) __VA_ARGS__)
#else
#define LOG_DEBUG(...) do { (void)0; } while (0)
#endif

// this define will surround whatever is passed into "name" with double quotes
#define stringify( name ) # name

const char* bitMaskStatusNames[] = {
    stringify( ok ),
    stringify( error )
};

const char* schedulerStateNames[] = {
    stringify( stopped ),
    stringify( running ),
    stringify( between ),
    stringify( paused )
};

const char* cycleTypeNames[] = {
    stringify( specificDays ),
    stringify( every2ndDay ),
    stringify( every3rdDay ),
    stringify( off ),
    stringify( invalidCycleType )
};

// for performing time calculations
#define DAY    86400000 // 86400000 milliseconds in a day
#define HOUR   3600000 // 3600000 milliseconds in an hour
#define MINUTE 60000 // 60000 milliseconds in a minute
#define SECOND 1000 // 1000 milliseconds in a second

/*****************************************************************************
 * Utility implementations
 ****************************************************************************/

const String bitFieldtoString(uint8_t bitField) {
    String s;
    bool multiple = false;

    for (uint8_t i = 0; i < 8; i++) {
        if (bitField & (1 << i)) {
            s += ((multiple) ? "," : "") + String(i + 1);
            multiple = true;
        }
    }

    return s;
}

void loadBitFieldToJsonArray(uint8_t bitField, JsonArray& a) {
    for (uint8_t i=0; i < 8; i++) {
        if (bitField & (1 << i)) {
            a.add(i + 1);
        }
    }
}

unsigned long getMidnightEpoch(NTPClient timeClient) {
    // now in seconds since 1/01/1970
    unsigned long nowEpoch = timeClient.getEpochTime();

    // get corresponding hours, minutes and seconds
    int h = timeClient.getHours();
    int m = timeClient.getMinutes();
    int s = timeClient.getSeconds();

    // remove hours, minutes and seconds from nowEpoch to obtain the epoch
    // time of midnight.
    unsigned long midnightEpoch = nowEpoch - (h * 60 * 60) - (m * 60) - s;

    return midnightEpoch;
}

String epochTimeAsString(unsigned long epochTime) {
    time_t tt = epochTime;
    struct tm* timeinfo = localtime(&tt);

    // should be enough for strings like Wed Aug 24 12:20:00 2022 (len 24)
    char buff[40];  

    // ULONG_MAX is the default setting when no cycle is set to run
    if (epochTime == ULONG_MAX) {
        return String("none");
    }
    
    // Generate:  Mon Aug 29 18:35 2022
    //            %a  %b  %d %H %M %Y
    strftime(buff, 39, "%a %b %d %H:%M %Y", timeinfo);

    return String(buff);
}


/*****************************************************************************
 * BitMaskItem implementations
 ****************************************************************************/

BitMaskItem_t::BitMaskItem(JsonArray zones) {
    bitMask = 0;
    status = ok;

    for (uint8_t z : zones) {
        bitMask |= 1 << (z - 1);
    }
}

BitMaskItem_t BitMaskItem_t::allZonesOn(uint8_t zoneCount) {
    uint8_t bitMask = 0;

    for (uint8_t z = 0; z < zoneCount; z++) {
        bitMask |= 1 << z;
    }
    return BitMaskItem_t(bitMask);
}

const String BitMaskItem_t::asString() const {
    String s = "BitMaskItem(";
    s += bitMask;
    s += ',';
    s += "\"";
    s += bitMaskStatusNames[status];
    s += "\")";
    return s;
}

/*****************************************************************************
 * ScheduleItem implementations
 ****************************************************************************/

/*
 * The bitMask is essentially an array of bits, so I have decided to switch to
 * using a representation more like ArduinoJson where if it is an array, even
 * with one item, it still looks like this: [3].  So a single  ScheduleItem_t 
 * properly stringified should look like this:  [[3],10], and if there are two
 * or more zones in the bitMask, then it should look like [[2,3],10]
 */

const String ScheduleItem::asString(float adj) const {
    String s = "[[";
    uint8_t items = 0;
    uint8_t adjustedRunTime = (uint8_t)((float)runTime * adj);

    // convert bitMask into a string like "2:6" where a colon delimits
    // multiple zones to turn on simultaneously

    for (uint8_t z = 1; z <= 8; z++) {
        if (bitMask & (1 << (z - 1))) {
            if (++items > 1) {
                s += ',';
            }
            s += z;
        }
    }

    s += "],";
    s += (adjustedRunTime < 1) ? 1 : adjustedRunTime;
    s += ']';
    return s;
}

/*****************************************************************************
 * CycleItem_t implementations
 ****************************************************************************/

/**
 * jsonArrayToBitField()
 * 
 * Utility function to convert a JSON array of integers into a a single
 * 8-bit bit field value.  For example, [1,2,3] = 7 where bits 1, 2 and 3
 * are flipped on — 0b00000111.  It is intended for use by the
 * fromJsonObject() method below for converting days of the week and
 * zone numbers into single 8-bit field values.
 */
uint8_t jsonArrayToBitField(const JsonArray& ja) {
    uint8_t bitField = 0;

    for (JsonVariant v : ja) {
        bitField |= 1 << (v.as<uint8_t>() - 1);
    }

    return bitField;
}

/**
 * CycleItem_t::fromJsonObject()
 * 
 * Deserializes a JSON object containing the definition of a CycleItem_t
 * into an instance of CycleItem_t.
 */
CycleItem CycleItem_t::fromJsonObject(JsonObject& jo) {
    CycleItem_t ci;

    strcpy(ci.cycleName, jo["name"].as<const char*>());

    // I guess I went with interpreting the type as a char* instead of a
    // String to reduce dynamic behavior... but that made the code below
    // look just a bit clumsy. :-(

    const char* typeName = jo["type"].as<const char*>();

    if (strcmp(typeName, cycleTypeNames[specificDays]) == 0)
        ci.cycleType = specificDays;
    else if (strcmp(typeName, cycleTypeNames[every2ndDay]) == 0)
        ci.cycleType = every2ndDay;
    else if (strcmp(typeName, cycleTypeNames[every3rdDay]) == 0)
        ci.cycleType = every3rdDay;
    else if (strcmp(typeName, cycleTypeNames[off]) == 0)
        ci.cycleType = off;
    else
        ci.cycleType = invalidCycleType;

    ci.daysBitField = jsonArrayToBitField(jo["days"].as<JsonArray>());
    ci.firstTimeDelay = jo["first"].as<uint8_t>();
    ci.startHour = jo["hour"].as<uint8_t>();
    ci.startMin = jo["min"].as<uint8_t>();
    ci.cycleCount = jo["count"].as<uint8_t>();

    // debugging to ensure that the schedule items array is coming through
    // as expected
    //
    // char tmp[1024];
    // serializeJson(jo["schedule"], tmp);
    // Serial.printf("jo['schedule']=%s\n", tmp);

    for (JsonArray jsi : jo["schedule"].as<JsonArray>()) {
        // debugging to see that each individual schedule item is being
        // deserialized correctly
        // serializeJson(jsi, tmp);
        // Serial.printf("jsi=%s\n", tmp);

        ci.scheduleItems.emplace_back(
            jsonArrayToBitField(jsi[0].as<JsonArray>()), 
            jsi[1]
        );
    }

    return ci;
}

/**
 * CycleItem_t::fromJsonString()
 * 
 * Fully deserializes a JSON String containing a cycle definition into a
 * CycleItem_t struct.  If the "schedule" item ends up deserializing
 * incorrectly, it is likly that the DynamicJsonDocument is too small.
 * The deserializeJson() function will fail silently and corrupt the last
 * schedule item.
 */
CycleItem CycleItem_t::fromJsonString(String& s) {
    CycleItem_t ci;
    DynamicJsonDocument doc(1024);

    LOG_DEBUG(
        "fromJsonString: s=%s\n", 
        s.c_str()
    );

    deserializeJson(doc, s);

    if (doc.overflowed()) {
        Serial.printf(
            "\n***** Error: DynamicJsonDocument overflowed "
            "in CycleItem_t::fromJsonString()\n"
            "doc.capacity()=%zu\n\n",
            doc.capacity()
        );
    }

    JsonObject jo = doc.as<JsonObject>();

    return CycleItem_t::fromJsonObject(jo);
}

/**
 * CycleItem_t::asString()
 * 
 * Converts a CycleItem_t into a String representation.  It uses the special
 * String allocation form to cause only one allocation that is reserved to a
 * specific length that should exceed most definitions and reduce heap
 * allocations, which should help reduce heap fragmination.  
 * 
 * I don't know if reserving on an 8-byte boundary is useful, but that is the
 * value I chose for reserve() below.
 */
String CycleItem_t::asString() {
    String s((char *)0);
    
        if (!s.reserve(144)) {
        return String("error");
    }

    s += cycleName;
    s += " type=";
    s += cycleTypeNames[cycleType];
    s += " days=";
    s += daysBitField;
    s += " [";
    s += bitFieldtoString(daysBitField);
    s += "] ";
    s += startHour;
    s += ":";

    if (startMin < 10) {
        s += "0";
    }

    s += startMin;
    s += " count=";
    s += cycleCount;
    s += " delay=";
    s += firstTimeDelay;
    s += " schd=[";
    
    if (!scheduleItems.empty()) {
        bool first = true;

        for (ScheduleItem_t& si : scheduleItems) {
            if (!first) s += ",";
            s += si.asString();
            first = false;
        }
    }

    s += "]";

    return s;
}

/*
    {
        "name":
        "type":
        "days": [int, ...]
        "first": firstTimeDelay
        "hour":
        "min":
        "count":
        "schedule": [[[int, ...], int], ...]
    }, ...
 */

String CycleItem_t::asJsonString() {
    DynamicJsonDocument doc(1024);

    doc["name"] = cycleName;
    doc["type"] = cycleTypeNames[cycleType];

    JsonArray days = doc.createNestedArray("days");
    loadBitFieldToJsonArray(daysBitField, days);

    doc["first"] = firstTimeDelay;
    doc["hour"] = startHour;
    doc["min"] = startMin;
    doc["count"] = cycleCount;

    JsonArray schedule = doc.createNestedArray("schedule");

    for (ScheduleItem_t si : scheduleItems) {
        JsonArray item = schedule.createNestedArray();
        JsonArray zones = item.createNestedArray();
        loadBitFieldToJsonArray(si.bitMask, zones);
        item.add(si.runTime);
    }

    String s((char *)0);
    
    if (!s.reserve(doc.size())) {
        return String("\"error\"");
    }

    serializeJson(doc, s);

    if (doc.overflowed()) {
        Serial.printf(
            "\n***** Error: DynamicJsonDocument overflowed "
            "in CycleItem_t::asJsonString()\n"
            "doc.capacity()=%zu\n\n",
            doc.capacity()
        );
    }

    return s;
}

/*****************************************************************************
 * SprinklerAPI implementations
 ****************************************************************************/

/**
 * Initialize the API, start the web server, restore CycleItems currently 
 * stored on-board and calculate the next cycle to start.
 * 
 * This function is intended to be invoked in the main sketch's setup().
 */
void SprinklerAPI::setup() {
    LOG_DEBUG("ESP resetReason: %s\n", ESP.getResetReason().c_str());

    pinMode(outputEnablePin, OUTPUT);
    checkOutputEnable();

    logMsgf("restarted|%s", ESP.getResetReason().c_str());

    if (ESP.getResetInfoPtr()->reason == REASON_EXCEPTION_RST) {
        logMsg(ESP.getResetInfo().c_str());
    }

    LOG_DEBUG("start - initializeUrls()\n");
    initializeUrls();
    LOG_DEBUG("end - initializeUrls()\n");

    server.begin();

    deserializeCycleItems();
    calcNextCycleStart();

    currDay = timeClient.getDay();
}

/**
 * SprinklerAPI controller main loop.
 * 
 * Capture the current time, determine if a cycle should run, and then service
 * the scheduler loop.
 */
void SprinklerAPI::loop() {
    now = millis();

    server.handleClient();

    if (now > nextEventMillis) {
        nextEventMillis = millis() + 1000L;
        if (!events.empty()) {
            std::function<void()> e = events.front();
            e();
            events.pop();
        }
    }

    if (shouldRunNextCycle()) {
        initiateNextCycle(); 
    }

    schedulerLoop();

    if (shouldSendStatusEvent) {
        sendStatusEvent();
    }
}

/**
 * SprinklerAPI::initializeUrls()
 * 
 * Declares all the URLs to which this server will respond.  So many URLs are
 * now being declared (albeit many of them are for testing purposes) that the
 * overall initialization phase of this class takes upwards of 5,000 ms.  This
 * seems scary, that maybe there need to be yields so that the watchdog does
 * not get upset.  Therefore after every 5 declarations, there is a manual
 * yield() inserted to give time back to the hardware.
 */
void SprinklerAPI::initializeUrls() {
    // this is vital to permit web pages loaded from other servers to access
    // this API -- without this, the other pages will encounter CORS errors

    server.enableCORS(true);

    if (fsAvailable) {
        server.serveStatic("/", LittleFS, "/index.html");
        server.serveStatic("/index.html", LittleFS, "/index.html");
        server.serveStatic("/sprinklers.js", LittleFS, "/sprinklers.js");
    } else {
        server.on(F("/index.html"), HTTP_GET, [this]() {
            sendMessage(
                "{\"status\": \"error\", "
                "\"msg\": \"LittleFS failed to begin()\"}"
            );
        });
    }

    // Each definition of a URL is augmented with a debugging statement to
    // confirm that it was defined correctly to debug a sitaution where the
    // server appeared not to recognize the URLs.  Now that they are here, I
    // decided to leave them in case they are useful in the future.  If any-
    // thing, they show progress through the initialization phase.

    LOG_DEBUG("/, /index.html\n");

    // the ESP8266WebServer's "on()" function requires pointers to what I
    // would describe as regular functions -- that is, not a function defined 
    // as a method on a class.  So in order to have a method in this class 
    // respond to a request, you have to wrap it in a lambda and capture
    // "this", which is a pointer to the current instance (allowing access to 
    // the references to the server and shift register objects).

    server.on(F("/status"), HTTP_GET, [this]() {
        sendApiStatus();
    });

    LOG_DEBUG("/status\n");

    server.on(UriBraces("/zone/{}/{}"), HTTP_GET, [this]() {
        events.push([this]() { controlZone(); });
        sendOkStatusMessage();
    });

    LOG_DEBUG("/zone/{}/{}\n");

    server.on(UriBraces("/toggle/{}"), HTTP_GET, [this]() {
        setToggleDelay();
        sendFormatted(
            "{\"status\": \"ok\", \"toggleDelay\": %lu}",
            toggleDelay
        );
    });

    LOG_DEBUG("/toggle/{}\n");

    server.on(F("/blink"), HTTP_GET, [this]() {
        blinkLed(3, 100, 200);
        sendOkStatusMessage();
    });

    LOG_DEBUG("/blink\n");

    yield();

    server.on(F("/restart"), HTTP_GET, [this]() {
        sendMessage("{\"status\": \"restarting\"}");
        delay(10);
        ESP.reset();
    });

    LOG_DEBUG("/restart\n");

    /*
     * Schedule API Paths
     *      /schd/1/20 = schedule zone 1 to run for 20 minutes 
     *                   (if this is the first scheduled item, it starts immediately)
     *                      • continue to invoke API for successive zone requests
     *                      • subsequent invocations add to vector this.schedule and increment this.scheduleCount
     *      /schd/pause = pause running the current schedule and temporarily turn off the scheduled zone
     *                      • also capture millis() into this.pausedScheduleItemMillis
     *      /schd/resume = turn on the scheduled zone again and resume the schedule
     *                      • scheduleItemEnd += (millis() - this.pausedScheduleItemMillis)
     *      /schd/cancel = turn off scheduled zone, flush schedule array, set currentScheduleItem = -1
     *      /schd/skip = go on to the next scheduled item immediately
     *      /schd/set = this is a POST request with a JSON payload that specifies the entire schedule
     *                      • this would cause a full cancel if a schedule is already runnin
     *                      • this fully resets the schedule
     * 
     * • use "toggle" to go from one zone to the next
     *   (this will ensure that if a zone is on when the schedule API is first invoked that it takes effect immediately)
     * • the contents of this.schedule should be included in the results returned by /status
     * • right now given the lack of EPROM persistence, it is understood that the contents of this.schedule will 
     *   be lost when a power cycle occurs (including invoking /restart)
     */

    server.on(UriBraces("/schd/{}"), HTTP_GET, [this]() {
        String action = server.pathArg(0);
        controlScheduler(action);
        sendOkStatusMessage();
    });

    LOG_DEBUG("/schd/{} (get)\n");

    server.on(UriBraces("/schd/{}/{}"), HTTP_GET, [this]() {
        const String zones = server.pathArg(0);
        const String runTime = server.pathArg(1);
        events.push([this, zones, runTime]() { scheduleItem(zones, runTime); });
        sendOkStatusMessage();
    });

    LOG_DEBUG("/schd/{}/{}\n");

    server.on(UriBraces("/schd/{}"), HTTP_POST, [this]() {
        // supports "set" and "append"
        events.push([this]() { schedulePost(); });
        sendOkStatusMessage();
    });

    LOG_DEBUG("/schd/{}/{} (post)\n");

    /*
     * Cycle API Paths
     *
     * /cycles{.json|.text}
     *      List all cycles
     * 
     * /cycle
     *      HTTP_POST = add or replace the cycle defined in the JSON body
     *      HTTP_DELETE = delete the cycle with the defined name
     * 
     * /cycle/{}
     *      Retrieve the cycle as a single JSON item
     * 
     * /cycle/{}/run
     *      Run the indicated cycle
     * 
     * /next-cycle
     *      Retrieve information about the next cycle to run
     */

    server.on(UriBraces("/cycles{}"), HTTP_GET, [this]() {
        String pa0 = server.pathArg(0);
        String content = getCyclesStatus(pa0);
        sendMessage(content.c_str());
    });

    LOG_DEBUG("/cycles{}\n");

    yield();

    /**
     * Handle adds and updates -- if an existing cycle is found with the same
     * name as the cycle sent in the body of the POST, then it will fully 
     * replace the previous one.
     */

    server.on(F("/cycle"), HTTP_POST, [this]() {
        String body = server.arg("plain");
        CycleItem_t ci = CycleItem_t::fromJsonString(body);
        String cycleName(ci.cycleName);
        String error = validateCycle(ci);

        if (error.length() > 0) {
            sendFormatted(
                "{\"status\": \"error\", \"msg\": \"invalid %s\"}",
                error.c_str()
            );
            return;
        }

        if (findCycle(cycleName)) {
            LOG_DEBUG(
                "replacing existing cycle found: %s\n",
                cycleName.c_str()
            );

            // don't recalc the next cycle because that wil be done by
            // addCycle() below

            deleteCycle(cycleName, false);
        }

        addCycle(std::move(ci));

        // this ends the request/response cycle initiated by the client
        sendMessage(getCyclesStatus().c_str());

        // this will be sent shortly later to update the UI
        triggerSendStatusEvent();
    });

    LOG_DEBUG("/cycle (post)\n");

    /**
     * The body of the delete should look like this:
     * 
     *      {"name": "Some Cycle"}
     */

    server.on(F("/cycle"), HTTP_DELETE, [this]() {
        String body = server.arg("plain");
        DynamicJsonDocument doc(64);

        deserializeJson(doc, body);

        LOG_DEBUG(
            "/cycle delete: %s\n",
            body.c_str()
        );

        if (doc.overflowed()) {
            Serial.printf(
                "\n***** Error: DynamicJsonDocument overflowed "
                "in on /cycle HTTP_DELETE\n"
                "doc.capacity()=%zu\n\n",
                doc.capacity()
            );
        }

        if (doc.containsKey("name")) {
            String cycleName = doc["name"].as<String>();

            if (findCycle(cycleName)) {
                deleteCycle(cycleName);

                // this ends the request/response cycle initiated by the client
                sendMessage(getCyclesStatus().c_str());

                // this will be sent shortly later to update the UI
                triggerSendStatusEvent();
            } else {
                sendFormatted(
                    "{\"status\": \"error\", "
                    "\"msg\": \"cycle '%s' not found\"}",
                    cycleName.c_str()
                );
            }
            return;
        } 

        sendMessage(
            "{\"status\": \"error\", "
            "\"msg\": \"JSON did not contain 'name' key\"}"
        );
    });

    LOG_DEBUG("/cycle (delete)\n");

    /**
     * Retrieve a cycle.  The name should be in the URL and it should be
     * URL encoded so that an exact match is possible (with spaces, etc)
     */

    server.on(UriBraces(F("/cycle/{}")), HTTP_GET, [this]() {
        String cycleName = server.urlDecode(server.pathArg(0));
                
        LOG_DEBUG("finding cycle: %s\n", cycleName.c_str());

        CycleItem_t* ci = findCycle(cycleName);

        if (ci) {
            LOG_DEBUG("cycle found: %s\n", ci->cycleName);
            server.send(200, "application/json", ci->asJsonString().c_str());
        } else {
            LOG_DEBUG("cycle not found\n");
            sendMessage("{\"status\": \"error\", \"msg\": \"cycle not found\"}");
        }
    });

    LOG_DEBUG("/cycle/{}\n");

    /**
     * Run a cycle on demand
     */

    server.on(UriBraces(F("/cycle/{}/run")), HTTP_GET, [this]() {
        String cycleName = server.urlDecode(server.pathArg(0));

        LOG_DEBUG("finding cycle to run: %s\n", cycleName.c_str());

        CycleItem_t* ci = findCycle(cycleName);

        if (ci) {
            LOG_DEBUG("cycle found\n");

            initiateCycle(ci);

            sendFormatted(
                "{\"status\": \"ok\", "
                "\"msg\": \"cycle started: %s\"}",
                ci->cycleName
            );
            return;
        }

        sendFormatted(
            "{\"status\": \"error\", "
            "\"msg\": \"cycle '%s' not found\"}",
            cycleName.c_str()
        );
    });

    LOG_DEBUG("/cycle/{}/run\n");

    /**
     * to-do - eliminate this API, it is not needed nor used
     * 
     * Return a message containing the next cycle to run.  While I am not
     * entirely certain I will need this in the UI, it is useful to be able to
     * see the results of a recalculation or the impact of a cycle 
     * modification.
     */

    server.on(F("/next-cycle"), HTTP_GET, [this]() {
        if (nextCycleItem) {
            sendFormatted(
                "{\"status\": \"ok\", "
                "\"nextCycle\": \"%s\", \"startEpoch\": %lu, "
                "\"startDateTime\": \"%s\"}",
                nextCycleItem->cycleName,
                nextCycleStartEpoch,
                getNextCycleStartAsString().c_str()
            );
        } else {
            sendMessage(
                "{\"status\": \"ok\", "
                "\"msg\": \"no cycle scheduled\"}"
            );
        }
    });

    LOG_DEBUG("/next-cycle\n");

    yield();

    /**
     * Log API Paths
     * 
     * /log/show
     *      Returns the entire log file
     * 
     * /log/reset
     *      Deletes the entire log file
     * 
     * /log/size
     *      Returns the size of the log
     * 
     * /log/mark/{}
     *      Enables the placement of a "mark" which is any arbitrary text
     *      obtained from the braces in the path.  Useful for putting some
     *      text in the log to signal that the lines following the mark are
     *      the result of some change or event.  Admittedly, not a feature
     *      that will see wide use, but it was useful at one time for 
     *      debugging purposes.
     */

    server.on(UriBraces("/log/{}"), HTTP_GET, [this]() {
        String pa0 = server.pathArg(0);

        if (pa0 == "show") {
            sendLog();
            return;
        } else 
        if (pa0 == "reset") {
            LittleFS.remove("/log.dat");
            LOG_DEBUG("removed '/log.dat'");
            sendMessage("{\"status\": \"ok\"}");
            return;
        } else
        if (pa0 == "size") {
            File f = LittleFS.open("/log.dat", "r");
            size_t s = f.size();
            f.close();
            sendFormatted("{\"status\": \"ok\", \"logSize\": %zu}", s);
        }
        else {
            sendServerUriNotFound();
        }
    });
    
    LOG_DEBUG("/log/{}\n");

    server.on(UriBraces("/log/mark/{}"), HTTP_GET, [this]() {
        String label = server.urlDecode(server.pathArg(0));
        logMsgf("mark|%s", label.c_str());
        sendLog();
    });

    LOG_DEBUG("/log/mark/{}\n");

    server.onNotFound([this]() {
        sendServerUriNotFound();
    });

    LOG_DEBUG("onNotFound\n");

    /**
     * API: /ls
     * 
     * Returns a listing of the root directory.  At this time there is no
     * provision to list subdirectories since SprinklersAPI doesn't make any.
     */
    server.on(F("/ls"), HTTP_GET, [this]() {
        String s((char *)0);
        bool first = true;

        if (!s.reserve(512)) {
            sendMessage(
                "{\"status\": \"error\", \"msg\": \"unable to allocate string\"}"
            );
            return;
        }

        s += "Directory of /:\n";

        for (Dir dir = LittleFS.openDir("/"); dir.next();) {
            if (!first) s += "\n";
            first = false;

            s += dir.fileName();
        }

        sendMessage(s.c_str());
    });

    LOG_DEBUG("/ls\n");

    /**
     * API: /download/{}
     * 
     * Returns to the client the exact, uninterpreted file contents of the
     * requested file.  Returns an error JSON message otherwise.
     */
    server.on(UriBraces(F("/download/{}")), HTTP_GET, [this]() {
        String fn = server.pathArg(0);
        File f = LittleFS.open(fn, "r");

        if (f) {
            server.streamFile(f, "text/plain");
            f.close();
        } else {
            sendFormatted(
                "{\"status\": \"error\", "
                "\"msg\": \"file '%s' not found\"}",
                fn.c_str()
            );
        }
    });

    LOG_DEBUG("/download/{}\n");

    yield();

    /**
     * API: /upload
     * 
     * Uploads a file to the on-board filesystem or replaces a file if it
     * already exists.  THERE IS NO RECOURSE FOR OVERWRITING A FILE.  
     * 
     * The primary purpose of this API is to permit updating the index.html
     * file.  But it also can be used by unit tests to temporarily completely
     * replace /cycles.json by first downloading the original version, then
     * uploading a new and specially constructed version (and then it would
     * need to invoke /calc to drive a recalculation of the next Cycle 
     * to run).
     * 
     * Example:
     * 
     * $ curl http://sptest.local/upload -F 'name=@data/index.html'
     * 
     * Notes:
     * 
     *  - do this from the project directory (i.e., NOT the "data" directory)
     *  - the "@" IS REQUIRED -- don't try leaving it off
     *  - sometimes (often) the upload fails -- just try it again and it
     *    probably will work fine
     *  - despite you thinking that Autosave is on, it doesn't always, so 
     *    just Cmd-S (Save) the file change before using curl to send it up
     * 
     * The reason this API call is better than using PlatformIO's
     * "Upload Filesystem Image" function is that you won't delete the cycles
     * which are defined locally on the board in the "cycles.json" file.
     */
    server.on(UriBraces(F("/upload")), HTTP_POST, 
        [this]() {
            // initial responder function -- my testing shows this function
            // can't return any text
            server.send(200);
        },
        [this]() {
            static File fsUploadFile;
            HTTPUpload& upload = server.upload();

            switch (upload.status) {
                case UPLOAD_FILE_START:
                    {
                        String filename = upload.filename;

                        if (!filename.startsWith("/")) filename = "/" + filename;

                        LOG_DEBUG(
                            "handleFileUpload Name: %s ",
                            filename.c_str()
                        );

                        fsUploadFile = LittleFS.open(filename, "w");
                    }
                    break;
                case UPLOAD_FILE_WRITE:
                    {
                        if (fsUploadFile) {
                            fsUploadFile.write(upload.buf, upload.currentSize);
                        }
                    }
                    break;
                case UPLOAD_FILE_END:
                    {
                        if (fsUploadFile) {
                            fsUploadFile.close();

                            LOG_DEBUG(
                                "handleFileUpload Size: %zu\n",
                                upload.totalSize
                            );

                            logMsgf(
                                "upload|%s|%zu", 
                                upload.filename.c_str(),
                                upload.totalSize
                            );

                            // these probably won't ever be used, but for a 
                            // complete implementation, these should be here
                            server.sendHeader("Location","/success.html");
                            server.send(303);
                        } else {
                            server.send(
                                500, 
                                "text/plain", 
                                "500: couldn't create file"
                            );
                        }
                    }
                    break;
                default:
                    {
                        server.send(
                            500,
                            "text/plain",
                            "500: error creating file"
                        );
                    }
            }
        }
    );

    LOG_DEBUG("/upload\n");

    /**
     * API: /rm/{}
     * 
     * Removes a file from the on-board filesystem.
     * THERE IS NO RECOURSE AFTER REMOVING A FILE, 
     * nor is there any confirmation.
     */
    server.on(UriBraces(F("/rm/{}")), HTTP_GET, [this]() {
        String fn = server.pathArg(0);

        if (!fn.startsWith(F("/"))) fn = "/" + fn;

        if (LittleFS.remove(fn)) {
            sendOkStatusMessage();
        } else {
            sendMessage(
                "{\"status\": \"error\", "
                "\"msg\": \"file not found\"}"
            );
        }
    });

    LOG_DEBUG("/rm/{}\n");

    /*
     * todo - delete this function when this kind of testing is no longer needed
     */
    server.on(F("/shouldRun"), HTTP_GET, [this]() {
        bool val = shouldRunNextCycle();
        sendFormatted("%s",(val) ? "true" : "false");
    });

    LOG_DEBUG("/shouldRun\n");

    /**
     * /calc API - causes the calcNextCycleStart() function to be invoked.
     * 
     * This API is useful for testing purposes, so it will be retained.
     * Unlike most APIs, it returns the cycles' status message, which makes
     * sense since we are calculating the next cycle start, so we want to see
     * how that turned out.
     */
    server.on(F("/calc"), HTTP_GET, [this]() {
        calcNextCycleStart();
        sendMessage(getCyclesStatus().c_str());
    });

    LOG_DEBUG("/calc\n");

    /*
     * todo - delete this function when this kind of testing is no longer needed
     */
    server.on(F("/ser"), HTTP_GET, [this]() {
        serializeCycleItems();
        sendOkStatusMessage();
    });

    LOG_DEBUG("/ser\n");

    yield();

    /*
     * todo - delete this function when this kind of testing is no longer needed
     */
    server.on(F("/deser"), HTTP_GET, [this]() {
        deserializeCycleItems();
        sendOkStatusMessage();
    });

    LOG_DEBUG("/deser\n");

    /*
     * todo - delete this function when this kind of testing is no longer needed
     */
    server.on(UriBraces("/del/{}"), HTTP_GET, [this]() {
        String cycleName = server.urlDecode(server.pathArg(0));
        deleteCycle(cycleName);
        Serial.printf("delete: %s\n", cycleName.c_str());
        for (CycleItemIterator it = cycleItems.begin(); it != cycleItems.end(); it++) {
            Serial.println(it->cycleName);
        }
        sendMessage("ok");
    });

    LOG_DEBUG("/del/{}\n");

    /**
     * /clear API
     * 
     * Safely removes all cycles from the controller.  While not generally
     * useful to the UI, this is useful for API testing purposes, so it will
     * be retained permanently.
     */
    server.on(F("/clear"), HTTP_GET, [this]() {
        clearCycles();
        sendOkStatusMessage();
    });

    LOG_DEBUG("/clear\n");

    /*
     * function for use with testing out using another digital pin (like D0) on
     * the Wemos D1 mini board to see if I can inhibit all output from the shift
     * register by pulling the Output Enable (OE) pin high
     */
    server.on(UriBraces("/oe/{}"), HTTP_GET, [this]() {
        String oe = server.pathArg(0);

        pinMode(outputEnablePin, OUTPUT);

        if (oe.equals("off") || oe.equals("high")) {
            digitalWrite(outputEnablePin, HIGH);
            Serial.printf("D0 (outputEnablePin=%u) set HIGH\n", outputEnablePin);
        } else if (oe.equals("on") || oe.equals("low")) {
            digitalWrite(outputEnablePin, LOW);
            Serial.printf("D0 (outputEnablePin=%u) set LOW\n", outputEnablePin);
        } else {
            sendFormatted("invalid: %s", server.uri().c_str());
            return;
        }
        sendFormatted("ok - /oe/%s", oe.c_str());
    });

    LOG_DEBUG("/oe/{}\n");

    server.on(F("/oe"), HTTP_GET, [this]() {
        uint8_t retval = (uint8_t)digitalRead(outputEnablePin);
        sendFormatted(
            "{\"status\": \"ok\", \"oe\": \"%s\"}", 
            (retval == 1) ? "off" : "on"
        );
    });

    LOG_DEBUG("/oe\n");

    yield();

    /* test API -- delete as soon as possible
     */
    server.on(UriBraces("/reg/{}"), HTTP_GET, [this]() {
        uint8_t val = (uint8_t)server.pathArg(0).toInt();
        uint8_t* retval = nullptr;
        shiftRegister.setAll(&val);
        checkOutputEnable();
        retval = shiftRegister.getAll();
        sendFormatted("ok - /reg/%u", val, *retval);
    });

    LOG_DEBUG("/reg/{}\n");

    /* test API -- delete as soon as possible
     */
    server.on(F("/reg"), HTTP_GET, [this]() {
        uint8_t* val = shiftRegister.getAll();
        sendFormatted("ok - getAll()=%u", *val);
    });

    LOG_DEBUG("/reg\n");

    server.on(UriBraces("/logic/{}"), HTTP_GET, [this]() {
        String mode = server.pathArg(0);
        uint8_t val;

        if (mode.equals("normal")) {
            setNormalLogic(true);
            val = 0;
        } else
        if (mode.equals("reversed")) {
            setNormalLogic(false);
            val = 255;
        }
        else {
            sendFormatted(
                "{\"status\": \"error\", \"msg\": \"invalid: %s\"}",
                mode.c_str()
            );
            return;
        }

        shiftRegister.setAll(&val);
        checkOutputEnable();
        sendOkStatusMessage();
    });

    LOG_DEBUG("/logic/{}\n");

    server.on(F("/logic"), HTTP_GET, [this]() {
        sendFormatted(
            "{\"status\": \"ok\", \"logic\": \"%s\"}",
            (getNormalLogic()) ? "normal" : "reversed"
        );
    });

    LOG_DEBUG("/logic\n");

    server.on(F("/adj"), HTTP_GET, [this]() {
        sendFormatted(
            "{\"status\": \"ok\", \"adj\": \"%u\"}",
            getSeasonalAdjustment()
        );
    });

    LOG_DEBUG("/adj\n");

    yield();

    server.on(UriBraces("/adj/{}"), HTTP_GET, [this]() {
        String adjString = server.pathArg(0);
        long adj = adjString.toInt();

        if (adjString.isEmpty()) {
            sendFormatted(
                "{\"status\": \"error\", \"msg\": \"invalid: %s\"}",
                "must supply seasonalAdjustment value"
            );
            return;
        }

        if (adj < 1 || adj > 255) {
            sendFormatted(
                "{\"status\": \"error\", \"msg\": \"invalid seasonalAdjust value: %s - %s\"}",
                adjString.c_str(),
                "must be between 1 and 255"
            );
            return;
        }

        setSeasonalAdjustment((uint8_t)adj);
        sendFormatted(
            "{\"status\": \"ok\", \"adj\": \"%u\"}",
            getSeasonalAdjustment()
        );
    });

    LOG_DEBUG("/adj/{}\n");

    server.on(F("/check"), HTTP_GET, [this]() {
        checkOutputEnable();
        sendOkStatusMessage();
    });

    LOG_DEBUG("/check\n");

    /* Set up the Server-Sent Event channel.  Invoker simply sends a GET
     * request to this URL and then the controller will send events to
     * that client until a new client is established.  Each time this URL is
     * invoked, the client will update to the new one.
     */
    server.on(F("/sse"), HTTP_GET, [this]() {
        sseClient = server.client();

        if (sseClient && sseClient.connected()) {
            LOG_DEBUG(
                "client connected - ip addr: %s\n",
                sseClient.remoteIP().toString().c_str()
            );

            // I referenced these two guides when coming up with my own
            // implementation:
            // https://github.com/IU5HKU/ESP8266-ServerSentEvents/blob/master/ESP8266_ServerSentEvents/ESP8266_ServerSentEvents.ino
            // https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events/Using_server-sent_events

            sseClient.println("HTTP/1.1 200 OK");
            sseClient.println("Content-Type: text/event-stream;charset=UTF-8");
            sseClient.println("Access-Control-Allow-Origin: *");  
            sseClient.println("Cache-Control: no-cache");  
            sseClient.println();
            sseClient.flush();

            // Align the ticker at the top of minute boundary so that a status
            // event message is always sent at the top of the new minute.
            // I chose to use localtime() to determine the seconds from the
            // retrieved epoch time instead of using NTPClient::getSeconds()
            // because the latter re-invokes getEpochTime() which takes the
            // movement of millis() into account.  I want the seconds to be
            // calculated directly off the epochTime value without drift, so
            // that can only be done using localtime().

            time_t tt = timeClient.getEpochTime();
            struct tm* t = localtime(&tt);
            int run_secs = 60 - t->tm_sec;
            char buff[11];  

            strftime(buff, 10, "%H:%M:%S", t);

            LOG_DEBUG("/sse time: %s\n", buff);
            LOG_DEBUG("sseTicker.once() in %i secs\n", run_secs);

            sseTicker.once(run_secs, [this]() {
                // send a status event now and then the next one will be sent
                // on the regular cycle of every minute

                triggerSendStatusEvent();

                sseTicker.attach(60, [this]() {
                    triggerSendStatusEvent();
                });
            });

            // immediately acknowledge the connection by sending a status
            // event message
            triggerSendStatusEvent();
        }
    });

    LOG_DEBUG("/sse\n");

    /**
     * /sse/{} API
     * 
     * This is a testing API, but probably worth keeping around.  It has two
     * purposes:  1) force the sending of a status event message, and
     * 2) manually terminate the sseTicker (which is useful for turning off
     * the automatic updates to the UI so that you can manually work with the
     * HTML of the UI in a web browser without updates constantly occurring
     * because the ticker is running).
     * 
     * Right now, if you make the parameter any value, it will just be logged,
     * unless you use the special value "stop", which is both logged and then
     * the sseTicker is terminated.
     */
    server.on(UriBraces("/sse/{}"), HTTP_GET, [this]() {
        String s = server.pathArg(0);

        LOG_DEBUG(
            "client connected = %s msg = %s\n", 
            (sseClient.connected()) ? "yes" : "no",
            s.c_str()
        );

        if (s.equals("stop")) {
            sseTicker.detach();

            sendCustomServerEvent("stop", (const char *)NULL);

            LOG_DEBUG("sseTicker detached\n");
        } else {
            triggerSendStatusEvent();
        }
        
        sendOkStatusMessage();
    });

    LOG_DEBUG("/sse/{}\n");

    /**
     * /hold API
     * 
     * GET /hold
     *      returns the current hold days
     * 
     * GET /hold/{}
     *      sets hold days as follows:
     *          0: hold not active
     *        > 0: do not run any cycle for the given number of days
     *        < 0: indefinite hold (typically just use -1)
     */

    server.on(F("/hold"), HTTP_GET, [this]() {
        String holdStr; 

        if (holdDays < 0) {
            holdStr = "system off";
        } else if (holdDays == 0) {
            holdStr = "no hold active";
        } else {
            holdStr = epochTimeAsString(holdEpoch);
        }

        sendFormatted(
            "{\"status\": \"ok\", \"holdDays\": %i, \"resume\": \"%s\"}",
            holdDays,
            holdStr.c_str()
        );
    });

    LOG_DEBUG("/hold\n");

    yield();

    server.on(UriBraces(F("/hold/{}")), HTTP_GET, [this]() {
        String holdVal = server.pathArg(0);

        if (holdVal.isEmpty()) {
            sendMessage(
                "{\"status\": \"error\", "
                "\"msg\": \"hold is an integer specifying days to pause system "
                "(-1 to turn off)\""
            );
            return;
        }

        setHoldDays(holdVal.toInt());

        String holdStr;

        if (holdDays < 0) {
            holdStr = "system off";
        } else if (holdDays == 0) {
            holdStr = "no hold active - system on";
        } else {
            holdStr = epochTimeAsString(holdEpoch);
        }

        LOG_DEBUG(
            "holdDays=%i holdEpoch=%lu (%s)\n", 
            holdDays, 
            holdEpoch,
            holdStr.c_str()
        );

        // sending a message back to the client terminates the request
        sendFormatted(
            "{\"status\": \"ok\", \"holdDays\": %i, \"resume\": \"%s\"}",
            holdDays,
            holdStr.c_str()
        );

        // these proceed now asynchronously from the client
        calcNextCycleStart();
        serializeCycleItems();
    });

    LOG_DEBUG("/hold/{}\n");

    /**
     * /debug/{} API
     * 
     * Injects a message straight into the Serial log.  Useful for debugging
     * unit tests -- by allowing the unit test to inject test method names
     * directly into the log, we can see which method is causing the board to
     * reset (which almost always is indicative of a pointer or allocation
     * bug that needs to be fixed).
     */

    server.on(UriBraces(F("/debug/{}")), HTTP_GET, [this]() {
        const String msg = server.pathArg(0);
        Serial.printf("\n%s\n\n", server.urlDecode(msg).c_str());
        sendOkStatusMessage();
    });

    LOG_DEBUG("/debug/{}\n");

    /**
     * /test/{}/{}/{} API
     * 
     * This is a test API that can be used to test getNextRunDayOffset().
     * It probably should be renamed and then used in unit tests (which
     * right now are implemented in the "python" directory).
     */

    server.on(UriBraces(F("/test/{}/{}/{}")), HTTP_GET, [this]() {
        uint8_t daysBitField = server.pathArg(0).toInt();
        int startDOW = server.pathArg(1).toInt();
        int offset = server.pathArg(2).toInt();
        int nextRunDayOffset = getNextRunDayOffset(daysBitField, startDOW, offset);
        sendFormatted(
            "daysBitField=%u startDOW=%i offset=%i nextRunDayOffset=%i\n",
            daysBitField,
            startDOW,
            offset,
            nextRunDayOffset
        );
    });

    LOG_DEBUG("/test/{}/{}/{}\n");

    server.on(UriBraces(F("/seek/{}")), HTTP_GET, [this]() {
        uint8_t pa0 = server.pathArg(0).toInt();
        File f = LittleFS.open("/seektest.dat", "w");

        if (f) {
            f.printf("line1\n");
            f.printf("line2\n");
        } else {
            sendMessage("unable to open '/seektest.dat'");
            return;
        }

        f.close();

        File f3 = LittleFS.open("/seektest.dat", "r+");
        // seek(0, SeekEnd) is at EOF.  Since we have "\n" at the end of each
        // string, seek(1, SeekEnd) positions us at the linefeed character of
        // the last line.  And seek(2, SeekEnd) positions us at the last visible
        // byte of the file, which is the '2' in the example below.  So, if we
        // wanted to get all the way back to the beginning of the file, we would
        // need to seek(12, SeekEnd).      
        //
        // Thus if we have this data:
        //
        //         111
        //         210987  <-- SeekEnd value
        // data:   line1\n
        //
        //         654321  eof = 0  <-- SeekEnd value
        // data:   line2\n
        f3.seek(pa0, SeekEnd);
        f3.printf("x");
        f3.close();

        /*
            this is a test function that sends /seektest.dat to the client
            to prove that the seek worked and that bytes were overwritten.
         */
        File f2 = LittleFS.open("/seektest.dat", "r");
        server.streamFile(f2, "text/plain");
        f2.close();
        
        /*
        okay, now I need to think a bit about my idea of having an interval function
        first of all that is idempotent -- it will do the same thing no matter how many
        times it is invoked.

        And what I want is for the function to put the current date at the bottom of the
        log file whenever the day changes.  But if the last message in the log file is a 
        date, then I want that date to be overwritten, because I don't want to waste log
        space if no other messages have been appended to the bottom of the file since the
        time that the date was written.

        The function is intended to be invoked using the Ticker class.  I would like
        for the timing to be accurate, so I think the interval will have to be every second.

        The function needs to be able to determine if the day has changed.  At minimum, I
        think this means I need a static variable that keeps track of the current day number.

        Then I need to invoke the time service somehow to figure out what day it is.  
        I could use the main NTPClient and pass that to this function.  It has a getDay()
        function that I can invoke and then watch for it to change.  However, Sunday is 0,
        so what do we initialize the static variable to?  getDay() returns and int, so since
        it demands a signed value, I can initialize it to -1.  

        There is a version of File::readBytes() that takes a char* buffer and a length.  So,
        if I know what I am looking for is ##/##/####, then I can read the last 10 bytes of
        the file and see if they match.  If they do, then I don't need to write anything.  If
        */
    });

    LOG_DEBUG("/seek/{}\n");

    server.on(UriBraces(F("/seektest/{}")), HTTP_GET, [this]() {
        static int lastDay = -1;
        uint8_t pa0 = server.pathArg(0).toInt();

        if (pa0 > 0) {
            lastDay = timeClient.getDay();

            char fbuff[11];
            File f = LittleFS.open("/seektest.dat", "r");

            // position to the first byte of the last line
            f.seek(6, SeekEnd);

            // read the last line except for the linefeed
            size_t bytesRead = f.readBytes(fbuff, 5);

            f.close();

            // null terminate the string so we can use strcmp()
            fbuff[bytesRead] = '\0';

            sendFormatted(
                "lastDay=%i fbuff=%s result=%s", 
                lastDay, 
                fbuff,
                strcmp(fbuff, "xine2") == 0 ? "match" : "no match"
            );
            return;
        }

        sendFormatted("lastDay=%i", lastDay);
    });

    LOG_DEBUG("/seektest/{}\n");

    yield();

    server.on(F("/now"), HTTP_GET, [this]() {
        time_t tt = timeClient.getEpochTime();
        struct tm* t = localtime(&tt);
        FSInfo64 fsinfo;

        LittleFS.info64(fsinfo);

        // When we calculate a percent available disk space, the canonical formula
        // is to divide the used bytes by the total bytes and multiply by 100.  But
        // that will always round down (because the result is truncated to an integer
        // type).  So, to accomplish a round up, we need to add 1/2 of 1% of the 
        // denominator.  Thus the rounding factor is calculated as follows.  It should
        // be added to the usedBytes before dividing by the totalBytes.

        uint64_t roundingFactor = fsinfo.totalBytes / 100 / 2;
        
        // I want a char buffer to hold a log timestamp that has this format:
        // YYYY-MM-DD HH:MM:SS
        // So that is 19 characters plus a null terminator, so 20 characters.
        // (Making it 25 gives a bit of buffer in case I am wrong.)

        char tsbuff[25];

        strftime(tsbuff, sizeof(tsbuff), "%y-%m-%d %H:%M:%S", t);
        sendFormatted(
            "log ts=%s epoch=%lli totalBytes=%llu usedBytes=%llu availableBytes=%llu (%i%%)",
            tsbuff, 
            tt,
            fsinfo.totalBytes,
            fsinfo.usedBytes,
            fsinfo.totalBytes - fsinfo.usedBytes,

            // to cause the percentage to round up, add roundingFactor to the numerator

            ((fsinfo.totalBytes - fsinfo.usedBytes + roundingFactor) * 100) / fsinfo.totalBytes
        );
    });

    LOG_DEBUG("/now\n");
}

bool SprinklerAPI::getNormalLogic() const {
    return normalLogic;
}

void SprinklerAPI::setNormalLogic(bool setting) {
    normalLogic = setting;
}

void SprinklerAPI::sendMessage(const char* s) const {
    String ss;
    if (ss.reserve(strlen(s) + 1)) {
        ss = s;
        ss += '\n';
    }
    server.sendHeader(String(F("Access-Control-Allow-Methods")), String(F("GET, POST, DELETE")));
    server.send(200, "text/plain", ss);
}

void SprinklerAPI::sendOkStatusMessage() const {
    sendMessage("{\"status\": \"ok\"}");
}

template<typename... Args> 
void SprinklerAPI::sendFormatted(const char* fmt, Args... vars) {
     sprintf(
        this->msg,
        fmt,
        vars...
    );
    sendMessage(this->msg);
}

void SprinklerAPI::sendServerUriNotFound() {
    sprintf(
        msg,
        "{\"status\": \"error\", \"msg\": \"not found: %s\"}", 
        server.uri().c_str()
    );
    sendMessage(msg);
}

void SprinklerAPI::sendLog() const {
    File f = LittleFS.open("/log.dat", "r");

    if (f) {
        server.streamFile(f, "text/plain");
        f.close();
    } else {
        sendMessage(
            "{\"status\": \"error\", "
            "\"msg\": \"file '/log.dat' not found\"}"
        );
    }
}

void SprinklerAPI::sendStatusEvent() {
    if (sseClient.availableForWrite()) {
        sseClient.printf(
            "data: {"
            "\"apiStatus\": %s"
            "}",
            getAPIStatus()
        );
        sseClient.println();
        sseClient.println();
        sseClient.flush();

        LOG_DEBUG(
            "sendStatusEvent(): event sent successfully to %s\n",
            sseClient.remoteIP().toString().c_str()
        );
    } else {
        LOG_DEBUG(
            "sendStatusEvent(): sseClient not available for write\n"
        );
        sseClient.stop();

        sseTicker.detach();
        LOG_DEBUG("sseTicker stopped\n");
    }
    shouldSendStatusEvent = false;
}

/**
 * SprinklerAPI::sendCustomServerEvent()
 * 
 * Permits the construction of a custom Server Sent Event.  A custom event
 * looks like this:
 * 
 *      event: event_name
 *      data: {"key": "json data"}
 *      id: some_id_value
 *      \n
 *      \n
 * 
 * The "id" portion of the message is optional, and there is no need for it
 * right now.
 * 
 * The main purpose for this function is to send a "stop" event.  For the
 * stop event, the event name is enough; data can be left empty (set it to
 * null).
*/
void SprinklerAPI::sendCustomServerEvent(const char* eventName, const char* data) {
    if (sseClient.availableForWrite()) {
        sseClient.printf(
            "event: %s\n"
            "data: %s\n",
            eventName,
            (data) ? data : "{}"
        );
        sseClient.println();
        sseClient.println();
        sseClient.flush();

        if (data) {
            LOG_DEBUG("sendCustomServerEvent('%s', '%s')\n", eventName, data);
        } else {
            LOG_DEBUG("sendCustomServerEvent('%s', null)\n", eventName);
        }
    } else {
        LOG_DEBUG("sendCustomServerEvent('%s') ignored\n", eventName);
    }
}

void SprinklerAPI::triggerSendStatusEvent() {
    shouldSendStatusEvent = true;
    LOG_DEBUG("triggerSendStatusEvent() called\n");
}

char* SprinklerAPI::getAPIStatus() {
    FSInfo64 fsinfo;
    uint64_t roundingFactor;
    uint8_t* registers = shiftRegister.getAll();
    uint8_t mask;
    String zones;
    zones.reserve(5); // only planning on 2 zones ever:  [2,6]
    
    LittleFS.info64(fsinfo);

    // When we calculate a percent available disk space, the canonical formula
    // is to divide the used bytes by the total bytes and multiply by 100.  But
    // that will always round down (because the result is truncated to an integer
    // type).  So, to accomplish a round up, we need to add 1/2 of 1% of the 
    // denominator.  Thus the rounding factor is calculated as follows.  It should
    // be added to the usedBytes before dividing by the totalBytes.

    roundingFactor = fsinfo.totalBytes / 100 / 2;

    // Construct a string that looks like this:  [1, 3, 4]  
    // if zones 1, 3 and 4 were on.  It should be a JSON compatible represen-
    // tation of the zones current running.  In practice, it will probably
    // only ever look like this: [1], because multiple zones won't run
    // at the same time.

    for (uint8_t i=0; i < 8; i++) {
        mask = 1 << i;

        if (normalLogic) {
            if (*registers & mask) {
                if (zones.length() > 0) {
                    zones += ",";
                }
                zones += String(i + 1);
            }
        } else {
            // The serial register works in reverse:  if and-ing with the mask
            // yields true (i.e., the bit is on) then the zone is actually off.
            // So to flip this, the result is then not-ed.

            if (!(*registers & mask)) {
                if (zones.length() > 0) {
                    zones += ",";
                }
                zones += String(i + 1);
            }
        }
    }

    // Construct a JSON representation of the schedule that looks like this:
    // [[1, 20], [2, 25], [3, 10]]
    // representing three ScheduleItem_t structs for zones 1, 2 and 3, having
    // runTime values of 20, 25 and 10, respectively.

    String schd = "[";

    if (!schedule.empty()) {
        // believe it or not, this constructs a copy of the actual "schedule"
        // instance variable so that we can iterate through it... the 
        // std::queue doesn't allow standard iteration over it using a "for
        // iterator" or a "for loop", so we approach it by using the front()
        // item, then popping it off and continuing until the queue is empty

        std::queue<ScheduleItem_t> q = schedule;
        
        while (!q.empty()) {
            schd += q.front().asString();
            q.pop();
            if (!q.empty()) {
                schd += ",";
            }
        }
    }
    schd += "]";

    File f = LittleFS.open("/log.dat", "r");
    size_t logSize = f.size();
    f.close();

    sprintf(
        msg, 
        "{"
        "\"status\": \"ok\", "
        "\"time\": \"%s\", "
        "\"freeHeap\": %lu, "
        "\"heapFragmentation\": %u, "
        "\"availableDiskSpace\": \"%llu (%llu%%)\", "
        "\"logicMode\": \"%s\", "
        "\"outputEnable\": \"%s\", "
        // current shift register info
        "\"registers\": %u, "
        "\"on\": [%s], "
        // scheduler info
        "\"siRemaining\": %i, "
        "\"now\": %lu, "
        "\"scheduleItemEnd\": %lu, "
        "\"schedule\": %s, "
        "\"scheduleSize\": %zu, "
        "\"schedulerState\": \"%s\", "
        "\"currCycle\": \"%s\", "
        "\"nextCycle\": \"%s\", "
        "\"startDateTime\": \"%s\", "
        "\"adj\": %u, "
        "\"holdDays\": %i, "
        "\"holdEpoch\": %lu, "
        "\"resume\": \"%s\", "
        // log info
        "\"logSize\": %zu, "
        // config info
        "\"numZones\": %u, "
        "\"toggleDelay\": %lu, "
        // host info
        "\"addr\": \"%s\", "
        "\"hostname\": \"%s\", "
        "\"upTime\": \"%s\", "
        "\"rssi\": %d, "
        "\"sketchSize\": %lu, "
        "\"freeSketchSpace\": %lu, "
        "\"bootVersion\": %u, "
        "\"chipId\": %" PRIu32 ", "
        "\"resetReason\": \"%s\""
        "}", 
        timeClient.getFormattedTime().c_str(),
        (unsigned long)ESP.getFreeHeap(),
        (unsigned int)ESP.getHeapFragmentation(),
        fsinfo.totalBytes - fsinfo.usedBytes,
        ((fsinfo.totalBytes - fsinfo.usedBytes + roundingFactor) * 100) / fsinfo.totalBytes,
        (getNormalLogic()) ? "normal" : "reversed",
        (digitalRead(outputEnablePin) == HIGH) ? "off" : "on",
        *registers, 
        zones.c_str(),
        getScheduledItemRemainingTime(),
        now,
        scheduleItemEnd,
        schd.c_str(),
        schedule.size(),
        schedulerStateNames[schedulerState],
        (runningCycleItem) ? runningCycleItem->cycleName : "",
        (nextCycleItem) ? nextCycleItem->cycleName : "",
        getNextCycleStartAsString().c_str(),
        getSeasonalAdjustment(),
        holdDays,
        holdEpoch,
        (holdDays > 0) ? epochTimeAsString(holdEpoch).c_str() :
            (holdDays == 0) ? "system on" : "system off",
        logSize,
        numberOfZones,
        toggleDelay,
        WiFi.localIP().toString().c_str(),
        DEVICE_NAME,
        getUpTime().c_str(),
        WiFi.RSSI(),
        (unsigned long)ESP.getSketchSize(),
        (unsigned long)ESP.getFreeSketchSpace(),
        ESP.getBootVersion(),
        ESP.getChipId(),
        ESP.getResetReason().c_str()
    );

    return msg;
}

void SprinklerAPI::sendApiStatus() {
    sendMessage(getAPIStatus());
}

void SprinklerAPI::sendInvalidZonesError(const String& zones) {
    sprintf(
        msg,
        "{\"status\": \"error\", \"msg\": \"invalid zones=%s\"}",
        zones.c_str()
    );
    sendMessage(msg);
}

void SprinklerAPI::controlZone() {
    String zones = server.pathArg(0);
    String command = server.pathArg(1);
    BitMaskItem_t mask = zonesToBitMask(zones);

    if (mask.status == error) {
        sendInvalidZonesError(zones);
        return;
    }

    if (command == "on") {
        turnZonesOn(mask.bitMask);
    }
    else if (command == "off") {
        turnZonesOff(mask.bitMask);
    }
    else if (command == "toggle") {
        uint8_t* registers = shiftRegister.getAll();

        // handle turning other zones off and delaying only if at least one
        // zone is currently on (255 indicates no zones on)

        // *** important note ***
        // 
        // If you want to continue using the "toggle" concept with the 
        // ESPAsyncWebServer, then you'll have to come up with an alternative
        // implementation of a delay() because you can't use it with that
        // framework's callbacks.

        if (*registers != 255) {
            turnAllZonesOff();
        }

        turnZonesOn(mask.bitMask);
    }
    else {
        sendServerUriNotFound();
        return;
    }
}

void SprinklerAPI::turnZonesOn(uint8_t bitMask) {
    uint8_t* digitalValues = shiftRegister.getAll();
    uint8_t newDigitalValues;

    if (normalLogic) {
        newDigitalValues = *digitalValues | bitMask;
    } else {
        newDigitalValues = *digitalValues & ~bitMask;
    }

    shiftRegister.setAll(&newDigitalValues);
    logZoneOp(bitMask, "on");
    checkOutputEnable();
    triggerSendStatusEvent();
}

void SprinklerAPI::turnZonesOff(uint8_t bitMask) {
    uint8_t* digitalValues = shiftRegister.getAll();
    uint8_t newDigitalValues;

    if (normalLogic) {
        newDigitalValues = *digitalValues & ~bitMask;
    } else {
        newDigitalValues = *digitalValues | bitMask;
    }

    shiftRegister.setAll(&newDigitalValues);
    logZoneOp(bitMask, "off");
    checkOutputEnable();
    triggerSendStatusEvent();
}

void SprinklerAPI::turnAllZonesOn() {
    if (normalLogic) {
        shiftRegister.setAllHigh();
    } else {
        shiftRegister.setAllLow();
    }

    logZoneOp("all", "on");
    checkOutputEnable();
    triggerSendStatusEvent();
}

void SprinklerAPI::turnAllZonesOff(bool requestStatusEvent) {
    if (normalLogic) {
        shiftRegister.setAllLow();
    } else {
        shiftRegister.setAllHigh();
    }

    logZoneOp("all", "off");
    checkOutputEnable();

    if (requestStatusEvent) {
        triggerSendStatusEvent();
    }
}

/**
 * SprinklerAPI::checkOutputEnable()
 * 
 * The OutputEnable pin of the shiftRegister works in reverse logic.  Set it
 * to HIGH to disable output on the register pins.  When OE gets low voltage,
 * the output on the register pins works normally.
 * 
 * This method should be invoked wherever any shiftRegister "set" function
 * is invoked.
 * 
 * This has to be compared to the normalLogic ("logic mode") of the system.
 * If logic is normal, then OE works normally.  That is, if the register is 0,
 * then that means in normal logic mode all pins are off, so squelch the 
 * register pins to (hopefully) prevent any voltage leakage.  (My hope is that
 * this will help eliminate run-away zones that seem to be turned on despite
 * the register value being 0.)  If logic is reversed, then it seems like
 * current should always be allowed to flow out the pins because this seems to
 * be the way the mechanical relay board works right now in the deployed 
 * controller.  (To turn off output would mean all relays would open.  This
 * doesn't seem right electrically, but that is what is out there right now.)
 */
void SprinklerAPI::checkOutputEnable() {
    uint8_t* reg = shiftRegister.getAll();

    if (normalLogic && *reg == 0) {
        digitalWrite(outputEnablePin, HIGH);
        LOG_DEBUG("checkOutputEnable: reg=%u oePin=%u -- setting HIGH\n", *reg, outputEnablePin);
    } else {
        digitalWrite(outputEnablePin, LOW);
        LOG_DEBUG("checkOutputEnable: reg=%u oePin=%u -- setting LOW\n", *reg, outputEnablePin);
    }
}

void SprinklerAPI::setToggleDelay() {
    toggleDelay = (unsigned long)server.pathArg(0).toInt();
}

/**
 * SprinklerAPI::blinkLed()
 * 
 * Blink the LED on the ESP8266 board.  This is useful both for the heartbeat blink
 * (which is a very short blink every 5 seconds), and for the long three blinks that
 * happen at the end of the main setup() function.  The short hearbeat blink is
 * accomplished by using Ticker that is set to run every five seconds.
 * 
 * Note that it almost doesn't make sense that this is part of the SprinklerAPI class.
 * Instead, it probably should either be in its own namespace module or simply in the
 * main sketch file.  But, it's here for now.
*/
void SprinklerAPI::blinkLed(uint8_t count, uint64_t onDuration, uint64_t offDuration) {
    pinMode(LED_BUILTIN, OUTPUT);

    for (uint8_t i=0; i < count; i++) {
        digitalWrite(LED_BUILTIN, LOW);
        delay(onDuration);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(offDuration);
    }
}

/**
 * SprinklerAPI::logDate()
 * 
 * Appends the current date to the end of the log file.  If the last line of the log
 * is a date, then no messages have been logged to the file since the time the previous
 * date was appended, so then the date is overwritten with the new date.  This function
 * is designed to log the date as close to midnight as possible so that subsequent log
 * messages will be associated with the correct date.
 * 
 * Parameters:
 * mode - 0 = setup, 1 = loop
*/
void SprinklerAPI::logDate(uint8_t mode) {
    if (mode == 0) {
        // setup mode
    } else {
        // loop mode
    }
}

const String SprinklerAPI::getUpTime() const {
    unsigned long upMillis = millis() - startedMillis;
    int days = upMillis / DAY ;                                //number of days
    int hours = (upMillis % DAY) / HOUR;                       //the remainder from days division (in milliseconds) divided by hours, this gives the full hours
    int minutes = ((upMillis % DAY) % HOUR) / MINUTE ;         //and so on...
    int seconds = (((upMillis % DAY) % HOUR) % MINUTE) / SECOND;
    String s;
    s.reserve(13);  //999d 23:59:59...

    // didn't check outcome of s.reserve() because at 13, it will stay
    // on the stack for sure

    if (days > 0) {
        s += days;
        s += "d ";
    }

    s += hours;
    s += ':'; 
    if (minutes < 10) s += "0";
    s += minutes;
    s += ':';
    if (seconds < 10) s += "0";
    s += seconds;

    // I did rather extensive free heap analysis and determined that it is
    // safe to return a String that is stack allocated.  C++ appears to be
    // able to handle returning the object -- I'm not sure, but perhaps "move
    // semantics" make this possible.  No matter what, clearly though the
    // String must be stack allocated because it is a local item, it is being
    // returned without issue and doesn't cause a memory leak because there is
    // no "delete s" in the invoking method.  In fact, over time, free heap
    // actually tends to float upwards, which is crazy.

    return s;
}

void SprinklerAPI::controlScheduler(const String& action) {
    ScheduleItem_t& si = schedule.front();

    logMsgf("schd|%s", action.c_str());

    if (action == "cancel") {
        // clear runningCycleItem here so that when turnAllZonesOff()
        // is done, it will send a properly updated status event

        runningCycleItem = nullptr;
        schedulerState = stopped;

        // clear the dequeue properly
        while (!schedule.empty()) {
            schedule.pop();
        }

        // wait till the end of the function to invoke turnAllZonesOff()
        // so that the entire updated context of the Scheduler will be ready
        // to be sent in the status event.

        turnAllZonesOff();
    } else 
    if (action == "pause") {
        pausedScheduleItemMillis = now;
        schedulerState = paused;
        turnZonesOff(si.bitMask);
    } else 
    if (action == "resume") {
        scheduleItemEnd += (now - pausedScheduleItemMillis);
        schedulerState = running;
        turnZonesOn(si.bitMask);
    } else 
    if (action == "skip") {
        scheduleItemEnd = now;
    }
}

void SprinklerAPI::controlScheduler(const char* action) {
    // convenience function to permit programmatic control of scheduler by
    // using const char* actions so that invoking functions don't have to wrap
    // those in a String instance -- this function does the wrapping.

    String actionString(action);
    controlScheduler(actionString);
}

void SprinklerAPI::scheduleItem(const String& zones, const String& runTime) {
    uint8_t uintRunTime = runTime.toInt();
    BitMaskItem_t mask = zonesToBitMask(zones);

    // If the schedule is totally empty right now, we don't want to send a
    // status event message because the actual zone turn-on will do that, and
    // we don't want to send spurious status messages with the scheduler not
    // yet running — it makes the UI look bad and jumpy.  However, if the
    // schedule already has items on it, then we do want to send a message
    // because pure scheduling won't cause any other action to happen, and we
    // need to acknowledge to the user that their requested zone was scheduled.
    // We can't send the status event at this point, however, because the
    // schedule hasn't yet been augmented with the new Schedule Item.  So that
    // needs to happen first.
    
    bool requestStatusEvent = !schedule.empty();

    if (mask.status == error) {
        sendInvalidZonesError(zones);
        return;
    }

    schedule.emplace(mask.bitMask, uintRunTime);
    logMsgf("schd|%s|%u", zones.c_str(), uintRunTime);

    if (requestStatusEvent) {
        triggerSendStatusEvent();
    }
}

/**
 * to do:  is this API method even necessary?  It isn't used by the UI
 * 
 * I think it should be eliminated
 */
void SprinklerAPI::schedulePost() {
    String cmd = server.pathArg(0);
    String body = server.arg("plain");
    DynamicJsonDocument doc(1024);

    deserializeJson(doc, body);
    
    if (doc.overflowed()) {
        Serial.printf(
            "\n***** Error: DynamicJsonDocument overflowed "
            "in SprinklerAPI::schedulePost()\n"
            "doc.capacity()=%zu\n\n",
            doc.capacity()
        );
    }

    JsonArray newSchedule = doc["schedule"];

    if (cmd == "set") {
        while (!schedule.empty()) {
            schedule.pop();
        }

        // cause the previous schedule to be replaced
        schedulerState = stopped;
    } 
    else if (cmd != "append") {
        sendServerUriNotFound();
        return;
    }
    
    for (JsonArray si : newSchedule) {
        BitMaskItem_t mask;

        if (si[0].is<const char*>()) {
            String zones(si[0].as<const char*>());
            mask = zonesToBitMask(zones);
        } else if (si[0].is<uint8_t>()) {
            mask = zoneToBitMask(si[0].as<uint8_t>());
        } else if (si[0].is<JsonArray>()) {
            mask = BitMaskItem_t(si[0].as<JsonArray>());
        } else {
            sprintf(
                msg,
                "{\"status\": \"error\", "
                "\"msg\": \"invalid: \"%s\"}",
                body.c_str()
            );
            sendMessage(msg);
            return;
        }
        schedule.emplace(mask.bitMask, si[1].as<uint8_t>());
    }

    logMsgf(
        "schd|%s|%s",
        cmd.c_str(),
        doc["schedule"].as<String>().c_str()
    );

    // should this be happening here?  Can this even happen from the UI?
    // I feel like this is purely an API that would happen from a command-line.
    // And that said, should I then even keep this API at all if it could
    // never happen in real life?
    triggerSendStatusEvent();
}

void SprinklerAPI::schedulerLoop() {
    switch (schedulerState)
    {
    case stopped:
        if (!schedule.empty()) {
            ScheduleItem_t& si = schedule.front();

            // this is done to ensure that if a schedule is set while zones
            // are on manually, that they are all turned off so that the
            // schedule will totally take over; also, no status event should
            // be sent here because the correct status event will be sent
            // shortly by turnZonesOn()

            turnAllZonesOff(false);

            schedulerState = running;
            scheduleItemEnd = now + (si.runTime * 60 * 1000);

            turnZonesOn(si.bitMask);
        }
        break;
    case running:
        if (now > scheduleItemEnd) {
            ScheduleItem_t& si = schedule.front();

            // save the ScheduleItem_t's bitMask so that we can pop it off the
            // schedule and still have the bitMask to turn off... this is
            // is needed so that the turnZonesOff() reports the correct state
            // back to the UI when it sends its status event.

            uint8_t savedBitMask = si.bitMask;

            // if the schedule size is 1, then we are on the last Schedule 
            // Item, which means that when we turn it off below, the Schedule
            // will be empty, which means the scheduler state should now be
            // "stopped".  Otherwise it ought to be "between".  Invoking the
            // turnZonesOff() function is delayed till the end so that it will
            // send the status event.

            if (schedule.size() == 1) {
                schedulerState = stopped;
                runningCycleItem = nullptr;
                logMsg("end");
            } else {
                schedulerState = between;
                scheduleItemEnd = now + toggleDelay;
            }

            schedule.pop();
            turnZonesOff(savedBitMask);
        }
        break;
    case between:
        // it is sort of odd that the way to move to the next Schedule Item in
        // the schedule is to force the schedulerState to "stopped", but that
        // is how I made this loop work.  I am not actually interested in seeing
        // the stopped state in the UI, so triggerSendStatusEvent() is explicitly
        // NOT INVOKED here.  We want the "stopped" state to exist for only a
        // very brief amount of time for the next look to occur and the status
        // is then reported correctly by another turnZonesOn() invocation.

        if (now > scheduleItemEnd) {
            schedulerState = stopped;
        }
    default:
        break;
    }
}

void SprinklerAPI::setFsAvailable(bool val) {
    fsAvailable = val;
}

int SprinklerAPI::getScheduledItemRemainingTime() const {
    if (schedule.empty()) {
        return 0;
    }

    return (int)((((scheduleItemEnd - now) % DAY) % HOUR) / MINUTE) + 1;
}

uint8_t SprinklerAPI::getSeasonalAdjustment() const {
    return seasonalAdjustment;
}

void SprinklerAPI::setSeasonalAdjustment(uint8_t adj) {
    seasonalAdjustment = adj;
    logMsgf("adj|%u", adj);
}

const String SprinklerAPI::getNextCycleStartAsString() const {
    String start = epochTimeAsString(nextCycleStartEpoch);
    return start;
}

BitMaskItem_t SprinklerAPI::zoneToBitMask(uint8_t zone) {
    if (zone < 1 || zone > numberOfZones) {
        return BitMaskItem_t(0, error);
    }

    return BitMaskItem_t(1 << (zone - 1), ok);
}

BitMaskItem_t SprinklerAPI::zonesToBitMask(const String& zones) {
    uint8_t mask = 0;
    uint8_t reg = 0;

    // special handler for "all", which is the only non-numeric or delimiter
    // value permitted.  In this case, it turns on the defined number of 
    // zones.

    if (zones == "all") {
        return BitMaskItem_t::allZonesOn(numberOfZones);
    }
    
    // zone_copy will hold a copy of zones.c_str() because the intention is to
    // use strtok() which will modify zone_arg.  It would be an undesirable
    // side-effect for strtok() to modify zones.

    char zones_copy[zones.length() + 1];

    strcpy(zones_copy, zones.c_str());

    // Initialize the tokenizer to look for a ":".  The cast to (char *)
    // is required because c_str() returns (const char *) and strtok()
    // can't handle that because it actually will update the array with
    // null bytes.  This is okay, because we don't care if arg becomes
    // corrupted because it won't be used again in this method.

    char *z = strtok(zones_copy, ",");

    // while we have a token (which is 1-based zone number), process it
    
    while (z != NULL) {
        reg = atoi(z);

        // Because atoi() returns 0 if there is nothing but non-numeric
        // bytes in z, we can sense that there is an error in the zones
        // specification, because zones are 1-based.  If a 0 appears, then
        // the zones string is deemed invalid, an error status is set and
        // the struct returned immediately.

        if (reg == 0 || reg > numberOfZones) {
            return BitMaskItem_t(0, error);
        }

        mask |= 1 << (reg - 1);

        // important - subsequent invocations of strtok() pass NULL
        // to use the same string again

        z = strtok(NULL, ",");
    }

    return BitMaskItem_t(mask, ok);
}

void SprinklerAPI::logZoneOp(uint8_t bitField, const char* op) {
    String zone_str = bitFieldtoString(bitField);
    logZoneOp(zone_str.c_str(), op);
}

void SprinklerAPI::logZoneOp(const char* literal, const char* op) {
    uint8_t* digitalValues = shiftRegister.getAll();
    logMsgf("%s|%s|%u", op, literal, *digitalValues);
}

/**
 * SprinklerAPI::logMsgf()
 * 
 * Using the same function signature as sprintf(), this function logs custom
 * formatted log messages.  It first attempts to use the pre-allocated "msg"
 * buffer.  However, if that fails due to a very long string, it falls back
 * to a heap-allocated char tempBuffer and then makes a second attempt at
 * logging the message.
 */
void SprinklerAPI::logMsgf(const char* format, ...) {
    va_list args;

    // attempt first to construct the string in this->msg, capturing the
    // actual number of chars that would have been written, even if they
    // don't all fit (vsnprintf will not overflow)

    va_start(args, format);
    size_t actualLen = vsnprintf(this->msg, sizeof(this->msg), format, args);
    va_end(args);

    // If the actual length fit inside this->msg, then invoke logMsg() to
    // write the message out to the log file.  Otherwise, it would have 
    // overflowed, so temporarily construct (and then free) a buffer that 
    // will hold everything, invoke vsnprintf() again, and try logging the
    // message again.

    if (actualLen <= sizeof(this->msg) - 1) {
        logMsg(this->msg);
    } else {
        char* tempBuffer = new char[actualLen + 1];

        if (!tempBuffer) {
            logMsg("logMsgf failed to allocate tempBuffer");
            return;
        }

        va_start(args, format);
        vsnprintf(tempBuffer, actualLen + 1, format, args);
        va_end(args);
        logMsg(tempBuffer);

        delete[] tempBuffer;
    }
}

void SprinklerAPI::logMsg(const char* s) {
    // YYYY-MM-DD HH:MM:SS = 19 chars + 1 for null terminator = 20
    char ts[20];
    time_t tt = timeClient.getEpochTime();
    struct tm* t = localtime(&tt);
    File f = LittleFS.open("/log.dat", "a");

    strftime(ts, sizeof(ts), "%m%d %H%M%S", t);

    if (f) {
        f.printf("%s|%s\n", ts, s);
        f.close();
        Serial.printf("%s|%s\n", ts, s);
    } else {
        Serial.println("unable to open '/log.dat' in logMsg()");
    }
}

/**
 * addCycle()
 * 
 * When invoking this function, use this incantation:
 * 
 *      addCycle(std::move(ci))
 * 
 * This function invokes serializeCycleItems() immediately to persist the new
 * cycle to the /cycles.json file.  Then calcNextCycleStart() has to be 
 * invoked again to ensure that if the new cycle ought to be the next one to
 * run, that it will be properly considered.
 */
bool SprinklerAPI::addCycle(CycleItem_t&& ci) {
    cycleItems.push_back(ci);
    serializeCycleItems();
    calcNextCycleStart();
    return true;
}

/**
 * Validates that a CycleItem_t can be added or updated.
 * 
 * If successful, it will return an empty String otherwise the String will
 * contain the error message to return to the caller.  Currently, there is
 * no concept of a warning -- it is either successful or not.
 */
String SprinklerAPI::validateCycle(CycleItem_t& ci) {
    String msg;

    // loop through all existing cycles and make sure none of them has the
    // same start time as this cycle

    for (CycleItem_t& other: cycleItems) {
        // if the other cycle has the same name as this one, then we don't
        // need to validate its start time... presumably anything we do to
        // its start time is fine with itself, it is the other cycles with
        // different names which can't have the same start time

        if (strcmp(other.cycleName, ci.cycleName) == 0) {
            continue;
        }

        if (other.startHour == ci.startHour && other.startMin == ci.startMin) {
            msg = "duplicate start time";
            return msg;
        }

    }

    // startHour must be between 0 and 23 — since startHour is a unsigned
    // number, it can't be less than 0, so that doesn't have to be tested
    
    if (ci.startHour > 23) {
        msg = "start hour";
        return msg;
    }

    // startMin must be between 0 and 59

    if (ci.startMin > 59) {
        msg = "start minute";
        return msg;
    }

    // if cycleType == invalidCycleType (which is set already by 
    // CycleItem_t::fromJsonObject()) then format the error message

    if (ci.cycleType == invalidCycleType) {
        msg = "cycle type";
        return msg;
    }

    // firstTimeDelay must be between 0 and 6

    if (ci.firstTimeDelay > 6) {
        msg = "first time delay";
        return msg;
    }

    // cycleCount must be between 1 and 5 (very arbitrary)

    if (ci.cycleCount < 1 || ci.cycleCount > 5) {
        msg = "cycle count";
        return msg;
    }

    // loop through scheduleItems and verify some bits about them

    uint8_t maxZoneMask = 1 << (numberOfZones);

    for (ScheduleItem_t si: ci.scheduleItems) {
        // this is weird... basically, if numberOfZones is less than 8, then
        // we want to make sure that the user didn't include zone 8, other-
        // wise, all we need to make sure of is that the bitMask isn't 0 --
        // that is, at least one zone must have been supplied

        if (numberOfZones < 8) {
            if (si.bitMask & maxZoneMask) {
                msg = "max zone exceeded";
                return msg;
            }
        }

        if (si.bitMask == 0) {
            msg = "no zone specified or max zone exceeded";
            return msg;
        }

        if (si.runTime < 1 || si.runTime > 99) {
            msg = "zone run time";
            return msg;
        }
    }

    return msg;
}

/**
 * Finds the first CycleItem_t that matches the given cycleName.
 * 
 * If no matching CycleItem_t entry is found, the function returns a null
 * pointer.  A simple pointer is returned, not an iterator value.
 * 
 * @param cycleName a String containing the name to be found
 * @returns a pointer to the matching CycleItem_t entry
 */
CycleItem_t* SprinklerAPI::findCycle(String& cycleName) {
    LOG_DEBUG("findCycle(\"%s\")\n", cycleName.c_str());

    for (CycleItem_t& ci : cycleItems) {
        LOG_DEBUG("ci.cycleName=%s\n", ci.cycleName);
        LOG_DEBUG("'%s' (%zu) == '%s' (%zu): %s\n", 
            cycleName.c_str(),
            strlen(cycleName.c_str()),
            ci.cycleName,
            strlen(ci.cycleName),
            (strcmp(cycleName.c_str(), ci.cycleName) == 0) ? "true" : "false"
        );

        if (cycleName.equals(String(ci.cycleName))) {
            return &ci;
        }
    }
    return nullptr;
}

/**
 * Delete all cycles that match the given name
 * 
 * Although it shouldn't be possible for multiple cycle items to have the same
 * name, all will nevertheless be deleted because this loop will go through
 * the entire cycleItems vector
 * 
 * @param cycleName a String containing the name to be deleted
 * @returns nothing
 */
void SprinklerAPI::deleteCycle(String& cycleName, bool recalc) {
    for (CycleItemIterator it = cycleItems.begin(); it != cycleItems.end();) {
        if (cycleName.equalsIgnoreCase(it->cycleName)) {
            it = cycleItems.erase(it);
        } else {
            ++it;
        }
    }

    if (recalc) {
        serializeCycleItems();
        calcNextCycleStart();
    }
}

int getNextRunDayOffset(uint8_t daysBitField, int startDOW, int offset) {
    // startDOW = today's day of the week (e.g. Sat=6, Sun=0)
    int nextDOW;

    while (true) {
        nextDOW = (startDOW + offset) % 7;

        LOG_DEBUG(
            "getNextRunDayOffset() - offset=%i daysBitField=%u nextDOW=%i (1 << nextDOW = %u)\n",
            offset,
            daysBitField,
            nextDOW,
            1 << nextDOW
        );

        if (daysBitField & (1 << nextDOW)) {
            return offset;
        }
        offset++;
    }
}

/**
 * SprinklerAPI::calcNextCycleStart()
 * 
 * Calculates the next start date for each CycleItem_t and stores the one that
 * will occur the soonest to now.  This is done in a two-step process for
 * each CycleItem_t.  First, "startOffsetFromMidnight" is calculated.  This is
 * the time component of the start date and time.  The time is calculated
 * the same way for specificDays, every2ndDay or every3rdDay.  Then, starting
 * with midnight today, the startOffsetFromMidnight is added to see if this
 * yields a start epoch that is greater than now.  If it is, then we're done,
 * otherwise we have to getNextRunDayOffset() (which works right now only for
 * the "specificDays" CycleType) and perform the calculation again.  This is
 * repeated while the calculation is less than the nowEpoch.  Once the 
 * calculation exceeds nowEpoch, we are done and have a future start time.
 * 
 * Note that these comments are incomplete, as is the function below.  It
 * does not yet handle every2ndDay or every3rdDay CycleTypes.
 * 
 * VERY IMPORTANT NOTE:  the sequence of calculations below is very delicately
 * balanced and perfected only after a ton of testing.  Therefore, DON'T MOVE
 * ANY LINES AROUND without doing tons of testing.
 */
void SprinklerAPI::calcNextCycleStart() {
    // if holdDays is negative, then we are in an indefinite hold, which means
    // that no cycle should run — effectively, everything is turned off

    if (holdDays < 0) {
        // as stated below, setting these to max & null turns everything off
        nextCycleStartEpoch = ULONG_MAX;
        nextCycleItem = nullptr;
        return;
    }

    // now in seconds since 1/01/1970
    unsigned long nowEpoch = timeClient.getEpochTime();

    // if nowEpoch > holdEpoch, then the hold has expired and should be
    // deactivated (but only if holdEpoch was actually set to something
    // other than its initial value of 0UL -- this prevents a "hold|off"
    // message from being logged at system startup)

    if (nowEpoch > holdEpoch && holdEpoch != 0UL) {
        clearHold();
    }

    // a day in epoch time (which is the number of seconds in a day)
    const unsigned long dayOffsetEpochTime = 24L * 60L * 60;

    // When thinking about a hold for a certain number of days, this means
    // that no cycle can run until that date.  So there is no reason to
    // consider epochs prior to that.  Therefore, instead of starting the
    // calculation cycle at the nowEpoch time returned by timeClient, just
    // start the calculation at the holdEpoch.

    if (holdDays > 0) {
        nowEpoch = holdEpoch;

        LOG_DEBUG(
            "system held for %i days - resuming %s\n", 
            holdDays,
            epochTimeAsString(holdEpoch).c_str()
        );
    }

    // extract the time components from the nowEpoch, however it it now set
    // (either what was returned from the timeClient or the holdEpoch, if a
    // hold is currently in effect)

    time_t tt = nowEpoch;
    struct tm* timeinfo = localtime(&tt);

    // remove hours, minutes and seconds from nowEpoch to obtain the epoch
    // time of midnight

    unsigned long midnightEpoch = 
        nowEpoch - (timeinfo->tm_hour * 60 * 60) 
                 - (timeinfo->tm_min * 60) 
                 - timeinfo->tm_sec;

    // this is 0=Sun based, so no adjustment needed
    int currDOW = timeinfo->tm_wday;

    LOG_DEBUG(
        "midnightEpoch=%lu (%s)\n", 
        midnightEpoch, 
        epochTimeAsString(midnightEpoch).c_str()
    );

    // will be calculated for each CycleItem_t as the start time in seconds 
    // since midnight
    unsigned long startOffsetFromMidnight = 0L;

    // offset from today that is the next run day
    int nextRunDayOffset = 0;

    // start date & time in epoch seconds
    unsigned long thisCycleStartEpoch = 0L;

    // along with nextCycleItem being set to nullptr, when nextCycleStartEpoch
    // is set to ULONG_MAX, this signals there is no next start date time

    nextCycleStartEpoch = ULONG_MAX;
    nextCycleItem = nullptr;

    if (cycleItems.size() == 0) {
        LOG_DEBUG("nothing scheduled to run");
        return;
    }

    // loop through each of the cycleItems looking to see if there is now a
    // cycle that would start sooner than the nextCycleStartEpoch that was
    // already calculated

    for (CycleItem_t& ci : cycleItems) {
        if (ci.cycleType != specificDays) {
            LOG_INFO( 
                "cycleType '%s' not supported\n",
                cycleTypeNames[ci.cycleType]
            );
            continue;
        }

        // the number of seconds (for use in epoch calculations) from midnight
        // when this cycle should start on its run day

        startOffsetFromMidnight = 
            (ci.startHour * (60L * 60L)) + 
            (ci.startMin * 60L);

        LOG_DEBUG(
            "cycle: %s %u:%02u\n", 
            ci.cycleName,
            ci.startHour,
            ci.startMin
        );

        // Loop through the calculations until thisCycleStartEpoch is greater
        // than nowEpoch.  If nowEpoch is right now and the time for any 
        // cycle to start has passed, we need to keep recalculating until the
        // first result that is greater than nowEpoch.  Furthermore, if 
        // nowEpoch was advanced ahead to holdEpoch, then we need to keep
        // running the calculation loop until the result is greater than the
        // holdEpoch (which was put into nowEpoch, if the hold is active).
        //
        // If we are looping again, we want to advance the nextRunDayOffset
        // by a day so that we don't retry the same day.  This is what moves
        // us forward in time.  The getNextRunDayOffset() will take this day
        // and then adjust it to the actual next run day as defined by the
        // CycleItem's daysBitField.

        for (
            nextRunDayOffset = 0, thisCycleStartEpoch = 0; 
            thisCycleStartEpoch <= nowEpoch; 
            ++nextRunDayOffset
        ) {
            // calculate the actual next run day for this CycleItem
            nextRunDayOffset = getNextRunDayOffset(
                ci.daysBitField, 
                currDOW,
                nextRunDayOffset
            );

            thisCycleStartEpoch = midnightEpoch + 
                (nextRunDayOffset * dayOffsetEpochTime) + 
                startOffsetFromMidnight;

            LOG_DEBUG(
                "nextRunDayOffset=%i ci.daysBitField=%u thisCycleStartEpoch=%lu %s nowEpoch=%lu\n",
                nextRunDayOffset,
                ci.daysBitField,
                thisCycleStartEpoch,
                (thisCycleStartEpoch <= nowEpoch) ? "<=" : ">",
                nowEpoch
            );
        }

        // If the result of the calculation for the CycleItem's next
        // start epoch (thisCycleStartEpoch) has turned out to be less than
        // the nextCycleStartEpoch, then we have found a CycleItem whose
        // start time is sooner.  In that case, replace the global information
        // about the next cycle to run (nextCycleStartEpoch is the time that
        // the nextCycleItem is to start).

        if (thisCycleStartEpoch < nextCycleStartEpoch) {
            nextCycleStartEpoch = thisCycleStartEpoch;
            nextCycleItem = &ci;
        }
    }

    LOG_INFO(
        "next cycle to start: %s nextCycleStartEpoch %lu (%s)\n",
        nextCycleItem->cycleName,
        nextCycleStartEpoch,
        getNextCycleStartAsString().c_str()
    );
}

bool SprinklerAPI::shouldRunNextCycle() {
    unsigned long nowEpoch = timeClient.getEpochTime();

    // if a hold is active, check to see if nowEpoch exceeds holdEpoch, and
    // clear the hold if it does.  If the hold is still active, then don't
    // even bother looking at the nextCycleStartEpoch, even if it is set.

    if (holdEpoch > 0UL) {
        if (nowEpoch > holdEpoch) {
            clearHold();
        } else {
            return false;
        }
    }

    bool val = nextCycleItem && (nowEpoch > nextCycleStartEpoch);

    return val;
}

void SprinklerAPI::initiateCycle(CycleItem_t* ci) {
    String siLog;
    float_t runTimeAdj = (float)seasonalAdjustment / 100.0f;
    uint8_t adjustedRunTime;

    runningCycleItem = ci;

    for (ScheduleItem_t si : ci->scheduleItems) {
        if ((adjustedRunTime = si.runTime * runTimeAdj) < 1)
            adjustedRunTime = 1;

        schedule.emplace(si.bitMask, adjustedRunTime);
        siLog += si.asString(runTimeAdj);
    }

    logMsgf("cycle|start|%s|%s", ci->cycleName, siLog.c_str());
    triggerSendStatusEvent();
}

void SprinklerAPI::initiateNextCycle() {
    // When a cycle is initiated, if another one is already running, then its
    // schedule items will be appended to the one already running.  However,
    // the new cycle's name will be assumed.

    if (runningCycleItem) {
        Serial.println("appending to currently running cycle");
    }

    // update the currently running cycle item to the next one that has
    // already been identified

    runningCycleItem = nextCycleItem;
    nextCycleItem = nullptr;
    
    // initiating a cycle simply requires queueing the cycle's scheduleItems
    // to the schedule controller's schedule queue

    if (runningCycleItem) {
        initiateCycle(runningCycleItem);
    } else {
        Serial.println("initiateNextCycle() invoked but no nextCycleItem defined");
    }

    /* delay invoking calcNextCycleStart() by a second to allow sufficient
     * time to pass that it will calculate a new cycle to start rather than
     * the current one.  This is the safe way to do it without using the
     * delay() function which is prohibited if I start using the async
     * web server.  (Note, any delay could be specified by adding more millis
     * to nextEventMillis -- it is just my thought that 1 second is 
     * sufficient here because that is all that is needed to exceed the
     * current nowEpoch value).
     */
    events.push([this]() { nextEventMillis += 1000; });
    events.push([this]() { calcNextCycleStart(); });
}

void SprinklerAPI::cancelCycle() {
    // from the perspective of the current cycle, it can be canceled easily, 
    // because from the perspective of the larger controller, a cycle is
    // "run" by simply copying the CycleItem_t's scheduleItems to the
    // "schedule" queue

    controlScheduler("cancel");
    runningCycleItem = nullptr;
}

/*
    what is the JSON document to create?

    {
        "cycles": [
            {
                "name":
                "type":
                "days": [int, ...]
                "first": firstTimeDelay
                "hour":
                "min":
                "count":
                "isAct":
                "schedule": [[[int, ...], int], ...]
            }, ...
        ]
    }

    I decided not to serialize bitFields directly but turn them into Json
    arrays, even if there is only one member in them.  My reasoning is that
    it will be much easier to debug that kind of content than it is to have
    to think about bit field values and whether they represent the correct
    value or not when the intention is to have multiple bits on.  It's fine
    to conserve memory in the sketch with bitfields, but I don't see any
    real value in the serialized data as very little compression is achieved
    by using them.

    That said, I'm not even sure why I should conserve memory in the sketch
    with bitfields because they are fairly annoying.  What if I just use
    them where required (which is in controlling zones) but otherwise use
    std::vector<int> or even std::vector<uint8_t>
 */

void SprinklerAPI::serializeCycleItems() {
    DynamicJsonDocument doc(1024 * 4);
    JsonArray cycles = doc.createNestedArray("cycles");

    for (CycleItem_t ci : cycleItems) {
        JsonObject cij = cycles.createNestedObject();
        JsonArray schedule;

        cij["name"] = ci.cycleName;
        cij["type"] = cycleTypeNames[ci.cycleType];

        JsonArray dj = cij.createNestedArray("days");
        loadBitFieldToJsonArray(ci.daysBitField, dj);

        cij["first"] = ci.firstTimeDelay;
        cij["hour"] = ci.startHour;
        cij["min"] = ci.startMin;
        cij["count"] = ci.cycleCount;

        schedule = cij.createNestedArray("schedule");

        for (ScheduleItem_t si : ci.scheduleItems) {
            JsonArray item = schedule.createNestedArray();
            JsonArray zones = item.createNestedArray();
            loadBitFieldToJsonArray(si.bitMask, zones);
            item.add(si.runTime);
        }
    }

    doc["holdDays"] = holdDays;
    doc["holdEpoch"] = holdEpoch;

    File fp = LittleFS.open("/cycles.json", "w");
    serializeJson(doc, fp);
    fp.close();

    if (doc.overflowed()) {
        Serial.printf(
            "\n***** Error: DynamicJsonDocument overflowed "
            "in SprinklerAPI::serializeCycleItems()\n"
            "doc.capacity()=%zu\n\n",
            doc.capacity()
        );
    }
}

void SprinklerAPI::deserializeCycleItems() {
    File fp = LittleFS.open("/cycles.json", "r");
    DynamicJsonDocument doc(1024 * 4);
    deserializeJson(doc, fp);
    fp.close();

    if (doc.overflowed()) {
        Serial.printf(
            "\n***** Error: DynamicJsonDocument overflowed "
            "in SprinklerAPI::deserializeCycleItems()\n"
            "doc.capacity()=%zu\n\n",
            doc.capacity()
        );
    }

    cycleItems.clear();

    JsonArray cycles = doc["cycles"].as<JsonArray>();

    LOG_DEBUG("cycles:\n");

    for (JsonVariant jvc : cycles) {
        JsonObject joc = jvc.as<JsonObject>();
        CycleItem_t ci = CycleItem_t::fromJsonObject(joc);

        LOG_DEBUG("%s\n", ci.asString().c_str());

        cycleItems.push_back(ci);
    }

    holdDays = doc["holdDays"].as<int8_t>();
    holdEpoch = doc["holdEpoch"].as<unsigned long>();

    LOG_INFO("restored %zu cycles\n", cycleItems.size());
}

void SprinklerAPI::clearCycles() {
    cancelCycle();

    // return all of the cycle controller attributes to their initial values
    nextCycleItem = nullptr;
    runningCycleItem = nullptr;
    nextCycleStartEpoch = ULONG_MAX;
    cycleItems.clear();
    
    clearHold();
}

String SprinklerAPI::getCyclesStatus(String& resultType) const {
    String s((char *)0);
    bool first = true;
    std::vector<CycleItem_t> sortedCycles = cycleItems;

    if (!s.reserve(2048)) {
        return String(
            "{\"status\": \"error\", \"msg\": \"unable to allocate string\"}"
        );
    }

    std::sort(sortedCycles.begin(), sortedCycles.end(), 
        [](CycleItem_t& ci1, CycleItem_t& ci2) {
            return (strcmp(ci1.cycleName, ci2.cycleName) < 0);
        });

    if (resultType.equals(F(".text"))) {
        for (CycleItem_t ci : sortedCycles) {
            if (!first) s += "\n";
            s += ci.asString();
            first = false;
        }

        s += "\nholdDays: ";
        s += holdDays;

        if (nextCycleItem) {
            s += " nextCycle: ";
            s += nextCycleItem->cycleName;
            s += " start: ";
            s += nextCycleStartEpoch;
            s += " (";
            s += getNextCycleStartAsString();
            s += ")";
        }
    } else {

        /*****************************************************************
         * the ironic thing here is that I'm pretty sure instead of re-doing
         * the whole serialization thing, I should have just returned the
         * /cycles.json file because the intention is for the on-board file
         * always to reflect the cycles in memory because that has to be 
         * true in order for the board not to lose the cycles if it reboots
         * 
         * so really, all this code below was sort of dumb, as was the
         * "asJsonString", though perhaps it was good to write that so as
         * to prepare to refactor the monolithic method way below
         *****************************************************************/
        s += "{\"status\": \"ok\", ";
        s += "\"cycles\": [";

        for (CycleItem_t ci : sortedCycles) {
            if (!first) s += ",";
            s += ci.asJsonString();
            first = false;
        }

        s += "]";

        if (nextCycleItem) {
            s += ", \"nextCycle\": \"";
            s += nextCycleItem->cycleName;
            s += "\", \"startEpoch\": ";
            s += nextCycleStartEpoch;
            s += ", \"startDateTime\": \"";
            s += getNextCycleStartAsString();
            s += "\", \"time\": \"";
            s += timeClient.getFormattedTime();
            s += "\"";
        }

        s += ", \"holdDays\": ";
        s += holdDays;
        s += ", \"holdEpoch\": ";
        s += holdEpoch;
        s += "}";
    }
    return s;
}

String SprinklerAPI::getCyclesStatus() const {
    String s((char *)0);
    return getCyclesStatus(s);
}

/**
 * SprinklerAPI::setHoldDays()
 * 
 * Configures the controller to hold (suspend) cycle initiation until after
 * time lapses past holdEpoch.  The system is "turned off" (placed into an
 * indefinite hold) by passing -1 for holdDays.  The system is turned back
 * on (automated cycle initiation resumes) by setting holdDays to 0.
 * 
 * When -1 is passed in for holdDays, the holdEpoch value is set to 
 * ULONG_MAX.  This is the largest value that an unsigned long value can
 * store, and is theoretically the most distant time in the future that
 * conventional C++ ctime can handle (Feb 7, 2106)
 * 
 * Since holdDays and holdEpoch are persisted with the cycle items, once the
 * hold values are set on the SprinklerAPI instance, serializeCycleItems() is
 * invoked to make them permanent and survive a restart.
 */
void SprinklerAPI::setHoldDays(int8_t holdDays) {
    if (holdDays > 0) {
        this->holdDays = holdDays;
        this->holdEpoch = getMidnightEpoch(timeClient) + (holdDays * (24UL * 60UL * 60UL));
        logMsgf("hold|%i", holdDays);
    } else if (holdDays == 0) {
        clearHold();
    } else {
        this->holdDays = -1;
        this->holdEpoch = ULONG_MAX;
    }

    serializeCycleItems();
}

/**
 * SprinklerAPI::clearHold()
 * 
 * Clears a system hold.  By setting these values to zero, the system should
 * return to normal automatic operation.
 */
void SprinklerAPI::clearHold() {
    holdDays = 0;
    holdEpoch = 0UL;
    logMsg("hold|end");
}
