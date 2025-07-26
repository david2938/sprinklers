#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <ShiftRegister74HC595.h>
#include <LittleFS.h>
#include <NTPClient.h>
#include <queue>
#include <Ticker.h>

// for some reason these imports aren't needed, but I don't understand why
// so for now, I will leave them but commented out

// #include <vector>
// #include <functional>
// #include <algorithm>

/**
 * Utility Functions
 * 
 * These utility functions are too useful to be contained in any one class or
 * struct, and probably eventually should end up in their own file.  But for
 * convenience sake, for now they are here.
 */

const String bitFieldtoString(uint8_t bitField);
void loadBitFieldToJsonArray(uint8_t bitField, JsonArray& a);
unsigned long getMidnightEpoch(NTPClient timeClient);
String epochTimeAsString(unsigned long epochTime);
int getNextRunDayOffset(uint8_t daysBitField, int startDOW, int offset = 0);

/**
 * ScheduleItem_t
 * 
 * Define a Schedule Item, which consists of a bitMask of zones and a run time.
 * 
 * A maximum of 8 zones may be specified in this bitMask, with the LSB being
 * zone 1.  A runTime greater than 254 doesn't make sense, thus it uses
 * a uint8_t data type.
 */

typedef struct ScheduleItem {
    uint8_t bitMask;
    uint8_t runTime;

    ScheduleItem(uint8_t bitMask, uint8_t runTime): bitMask(bitMask), runTime(runTime) {}
    const String asString(float adj = 1.0f) const;
} ScheduleItem_t;

/**
 * BitMastStatus_t
 * 
 * An enum used to define the success of building a BitMaskItem_t.  One of
 * these values is returned in BitMaskItem.status.
 * 
 * Be sure to keep bitMaskStatusNames[] in sync in .cpp file (in the same
 * order as defined below).
 */

typedef enum BitMaskStatus {
    ok,
    error
} BitMaskStatus_t;

/**
 * BitMaskItem_t
 * 
 * A struct to contain a bitMask, which is an unsigned 8 bit integer where
 * each bit represents a zone, with the LSB being zone 1.  The status value
 * indicates whether or not the bitMask could be created successfully from
 * the input.
 */

typedef struct BitMaskItem {
    uint8_t bitMask;
    BitMaskStatus_t status;

    BitMaskItem(uint8_t bitMask, BitMaskStatus_t status): 
        bitMask(bitMask), status(status) {}
    BitMaskItem(uint8_t bitMask):
        bitMask(bitMask), status(ok) {}
    BitMaskItem(JsonArray zones);
    BitMaskItem() = default;
    static BitMaskItem allZonesOn(uint8_t zoneCount);
    const String asString() const;
} BitMaskItem_t;

// be sure to keep schedulerStateNames[] in sync in .cpp file
typedef enum SchedulerState {
    stopped,
    running,
    between,
    paused
} SchedulerState_t;

// be sure to keep cycleTypeNames[] in sync in .cpp file
// (order needs to be the same too).  The "manual" item is the way to provide
// the ability to turn off a cycle but retain the ability to run it whenever
// you want manually from the UI.

typedef enum CycleType {
    specificDays,
    every2ndDay,
    every3rdDay,
    off,
    invalidCycleType
} CycleType_t;

/*
 * todo - add an "active" boolean item so that you can have a definition
 *          but not always use it and not require that in order to 
 *          deactivate a schedule that you have to delete it
 */

typedef struct CycleItem {
    char cycleName[21];
    CycleType_t cycleType = specificDays;
    // Sunday occupies the least significant bit
    // 6543210
    // SFTWTMS
    // ARHEUOU
    // TIUDENN
    uint8_t daysBitField;
    uint8_t firstTimeDelay = 0;
    uint8_t startHour;
    uint8_t startMin = 0;
    uint8_t cycleCount = 1;
    std::vector<ScheduleItem_t> scheduleItems;

    // causes the default constructor to be implemented despite the presence
    // of other constructors
    CycleItem() = default;

    // instantiate a CycleItem by several key fields
    CycleItem(
        const char* name,
        uint8_t daysBitField,
        uint8_t startHour,
        uint8_t startMin
    ): cycleType(specificDays), 
        daysBitField(daysBitField),
        startHour(startHour),
        startMin(startMin)
        {
            memcpy(cycleName, name, 20);
        }
    // return various representations of a CycleItem
    String asString();
    String asJsonString();

    // instantiate CycleItem by deserialization
    static CycleItem fromJsonObject(JsonObject& jo);
    static CycleItem fromJsonString(String& s);
} CycleItem_t;

typedef std::vector<CycleItem_t>::iterator CycleItemIterator;

class SprinklerAPI {
    private:
        ESP8266WebServer& server;
        ShiftRegister74HC595<1>& shiftRegister;
        NTPClient& timeClient;
        uint8_t numberOfZones;

        // normalLogic indicates whether the board uses 0-based register math
        // or 255-based register math -- in other words, the "8 Solid State
        // Register" board uses 0-based register math, or "normal logic", and
        // the "8 Register Board" deployed in my system uses reverse (not 
        // normal) math where 255 is all off.  (This is probably due to an
        // electrical error I made in wiring up that register board, but it is
        // now assembled this way and so this code needs to be able to support
        // not normal logic until such time as I figure out what I did wrong.)

        bool normalLogic = true;

        // D0 is just the default and is expected to be changed by the value
        // being set correctly at construction time; in any case this should
        // be set to the pin that is connected to the shift register's
        // OutputEnable pin.

        uint8_t outputEnablePin = D0;
        unsigned long now = millis();
        char msg[1024];
        bool fsAvailable = true;
        // default delay on toggle set to 5 seconds
        unsigned long toggleDelay = 5000;

        // scheduler attributes

        SchedulerState_t schedulerState = stopped;
        std::queue<ScheduleItem_t> schedule;
        unsigned long scheduleItemEnd = 0L;
        unsigned long pausedScheduleItemMillis = 0L;
        unsigned long startedMillis = millis();

        // cycle controller attributes

        std::vector<CycleItem_t> cycleItems;
        unsigned long nextCycleStartEpoch = ULONG_MAX;
        CycleItem_t* nextCycleItem = nullptr;
        CycleItem_t* runningCycleItem = nullptr;
        uint8_t seasonalAdjustment = 100;
        int8_t holdDays = 0;
        unsigned long holdEpoch = 0UL;

        // event processing

        unsigned long nextEventMillis = 0L;
        std::queue<std::function<void()>> events;

        // logging/debugging
        
        int currDay = 0;

        // client for Server Sent Events
        WiFiClient sseClient;

        Ticker sseTicker;
        bool shouldSendStatusEvent = false;

    public:
        SprinklerAPI(
            ESP8266WebServer &server, 
            ShiftRegister74HC595<1>& shiftRegister,
            NTPClient& timeClient,
            uint8_t numberOfZones,
            uint8_t outputEnablePin
        ): server(server), 
            shiftRegister(shiftRegister), 
            timeClient(timeClient),
            numberOfZones(numberOfZones),
            outputEnablePin(outputEnablePin)
            {}

        void setup();
        void loop();
        void initializeUrls();
        bool getNormalLogic() const;
        void setNormalLogic(bool setting);
        char* getAPIStatus();
        void sendMessage(const char* s) const;
        void sendOkStatusMessage() const;
        template<typename... Args>
        void sendFormatted(const char* fmt, Args... vars);
        void sendApiStatus();
        void sendServerUriNotFound();
        void sendInvalidZonesError(const String& zones);
        void sendLog() const;
        void sendStatusEvent();
        void sendCustomServerEvent(const char* eventName, const char* data);
        void triggerSendStatusEvent();
        void controlZone();
        void turnZonesOn(uint8_t bitMask);
        void turnZonesOff(uint8_t bitMask);
        void turnAllZonesOn();
        void turnAllZonesOff(bool shouldSendStatusEvent = true);
        void checkOutputEnable();
        void setToggleDelay();
        void blinkLed(uint8_t count, uint64_t onDuration, uint64_t offDuration);
        void logDate(uint8_t mode);
        const String getUpTime() const;
        void controlScheduler(const String& action);
        void controlScheduler(const char* action);
        void scheduleItem(const String& zones, const String& runTime);
        void schedulePost();
        void schedulerLoop();
        int getScheduledItemRemainingTime() const;
        const String getNextCycleStartAsString() const;
        uint8_t getSeasonalAdjustment() const;
        void setSeasonalAdjustment(uint8_t adj);
        void setFsAvailable(bool val);
        BitMaskItem_t zoneToBitMask(uint8_t zone);
        BitMaskItem_t zonesToBitMask(const String& zones);

        // Logger methods

        void logZoneOp(uint8_t bitField, const char* op);
        void logZoneOp(const char* literal, const char* op);
        void logMsg(const char* msg);
        void logMsgf(const char* format, ...);

        // Cycle Controller methods

        bool addCycle(CycleItem_t&& ci);
        String validateCycle(CycleItem_t& ci);
        CycleItem_t* findCycle(String& cycleName);
        void deleteCycle(String& cycleName, bool recalc = true);
        /*
            - would be called by 
                setup()
                addCycle()
                updateCycle()
                deleteCycle()
                    because the above 3 all could change 
                    what is the next cycle to start
        */
        void calcNextCycleStart();
        bool shouldRunNextCycle();
        /*
            - mostly for documentation purposes, but this
              is intended to be invoked by loop()
        */
        void initiateNextCycle();
        void initiateCycle(CycleItem_t* ci);
        /*
            - delegates to scheduler, which is now
              possible because I just did a tiny 
              refactor which permits the passing in of
              a string that contains the desired action.
              Unfortunately, that said, it has to be an
              actual String() and not "cancel", so you'd
              have to programmtically invoke it as
              controlScheduler(String("cancel")), which
              looks kind of stupid.  But if I were to
              change it to (const char* action) to permit
              the invocation to look like
              controlScheduler("cancel"), then all of the
              tests have to become
              strcmp("cancel") == 0, which is not pretty
              either
        */
        void cancelCycle();
        void serializeCycleItems();
        void deserializeCycleItems();
        void clearCycles();
        String getCyclesStatus(String& resultType) const;
        String getCyclesStatus() const;
        void setHoldDays(int8_t holdDays);
        void clearHold();
};
