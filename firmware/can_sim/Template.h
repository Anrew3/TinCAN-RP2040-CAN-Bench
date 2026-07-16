#pragma once

#include <Arduino.h>
#include "Config.h"

// Button definition
struct ButtonDef {
    char name[16];          // e.g., "UP", "DOWN", "OK"
    byte data[8];           // CAN data bytes when pressed
};

// Gauge definition (RPM, Speed, Fuel, etc.)
struct GaugeDef {
    char name[16];          // e.g., "RPM", "SPEED"
    unsigned long canId;    // CAN ID to send
    byte base[8];           // Base message bytes
    byte valueBytes[2];     // Which bytes hold the value (e.g., [3,4] for RPM)
    byte numValueBytes;     // 1 or 2 bytes
    float scale;            // Multiply input by this
    int offset;             // Add this after scaling
    int minVal;             // Minimum allowed value
    int maxVal;             // Maximum allowed value
    unsigned long intervalMs; // Send interval
};

// Temperature sensor definition
struct TempSensorDef {
    char name[16];          // e.g., "COOLANT", "OIL"
    byte byteIndex;         // Which byte in the temp message
    // Formula: (tempF - 32) * 5 / 9 + offset
    int offset;             // Usually 60 for Ford
};

// Tire definition
struct TireDef {
    char name[8];           // e.g., "FL", "FR", "RL", "RR"
    byte byteIndex;         // Which byte in TPMS message
    float scale;            // PSI to kPa multiplier (usually 6.895)
};

// Blinker definition
struct BlinkerDef {
    byte leftByte;          // Which byte for left blinker
    byte leftMask;          // Bit mask for left
    byte rightByte;         // Which byte for right blinker
    byte rightMask;         // Bit mask for right
};

// Background message (keepalive, etc.)
struct BackgroundMsgDef {
    unsigned long canId;
    byte data[8];
    byte len;
    unsigned long intervalMs;
};

// Signal - a named, multi-state control that overlays a byte span onto an
// already-transmitted CAN frame. Fully template-defined so any indicator
// (warning lights, headlamp, day/night, doors, gear, backlight, ...) can be
// added without firmware changes. Each state writes `len` bytes starting at
// `startByte`. A SIGNAL:<name>:<n> command with a numeric value writes a
// single raw byte instead, for continuous controls like backlight.
struct SignalStateDef {
    char name[16];
    byte data[8];
    byte len;
};

struct SignalDef {
    char name[16];
    unsigned long canId;    // must be a frame the runner already transmits
    byte startByte;
    SignalStateDef states[MAX_SIGNAL_STATES];
    byte numStates;
    byte defaultState;      // index into states, or 0xFF for none
};

// Boot message - sent once at power-up / template load, in order.
// delayMs is the wait before this message is sent (relative to the
// previous one), so a sequence can pace a head unit's init handshake.
struct BootMsgDef {
    unsigned long canId;
    byte data[8];
    byte len;
    unsigned long delayMs;
};

// Complete Template structure
struct Template {
    // Metadata
    char id[32];            // Unique identifier
    char name[48];          // Display name
    char version[12];       // Version string
    bool isOfficial;        // true = cannot be deleted

    // Buttons
    unsigned long buttonCanId;
    unsigned long buttonHoldMs;
    unsigned long buttonIntervalMs;
    byte buttonDefault[8];
    ButtonDef buttons[MAX_BUTTONS];
    byte numButtons;

    // Gauges
    GaugeDef gauges[MAX_GAUGES];
    byte numGauges;

    // Temperature
    unsigned long tempCanId;
    byte tempBase[8];
    unsigned long tempIntervalMs;
    TempSensorDef tempSensors[MAX_TEMP_SENSORS];
    byte numTempSensors;

    // TPMS
    unsigned long tpmsCanId;
    byte tpmsBase[8];
    unsigned long tpmsIntervalMs;
    TireDef tires[MAX_TIRES];
    byte numTires;

    // Blinkers / body-lighting frame (0x3B3, mirrored to 0x3B2)
    unsigned long blinkerCanId;
    unsigned long blinkerCanIdAlt;  // Some vehicles use two IDs
    byte blinkerBase[8];            // "light" base - lighting/blinker overlays ride this
    unsigned long blinkerIntervalMs;
    unsigned long blinkerBlinkRateMs;
    BlinkerDef blinker;

    // Optional WAKE payload for the same frame. Fords need a CGEA/network
    // wake pattern on 0x3B3 to boot a dormant SYNC head unit; it collides
    // with the lighting bytes, so it's a separate base you switch to (BODY:WAKE
    // / BODY:LIGHT). When present + wakeDefault, the frame starts in wake mode.
    byte blinkerWakeBase[8];
    bool blinkerHasWake;
    bool blinkerWakeDefault;

    // VIN
    unsigned long vinCanId;
    unsigned long vinIntervalMs;
    byte vinFramePrefix;

    // Background messages
    BackgroundMsgDef backgroundMsgs[MAX_BACKGROUND_MSGS];
    byte numBackgroundMsgs;

    // Boot sequence (one-shot, in order, at power-up / template load)
    BootMsgDef bootMsgs[MAX_BOOT_MSGS];
    byte numBootMsgs;

    // Signals (named byte-span overrides on transmitted frames)
    SignalDef signals[MAX_SIGNALS];
    byte numSignals;
};

// Helper to initialize a template to defaults
void initTemplate(Template* t);

// Helper to find a gauge by name
GaugeDef* findGauge(Template* t, const char* name);

// Helper to find a button by name
ButtonDef* findButton(Template* t, const char* name);

// Helper to find a temp sensor by name
TempSensorDef* findTempSensor(Template* t, const char* name);

// Helper to find a tire by name
TireDef* findTire(Template* t, const char* name);
