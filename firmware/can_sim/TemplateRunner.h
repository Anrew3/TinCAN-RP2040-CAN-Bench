#pragma once

#include <Arduino.h>
#include "mcp_can.h"
#include "Template.h"

// Template Runner - executes CAN messages based on current template
class TemplateRunner {
public:
    TemplateRunner();

    // Initialize with CAN bus reference
    void begin(MCP_CAN* can);

    // Set the template to run
    void setTemplate(Template* t);

    // Main tick function - call from loop()
    void tick(unsigned long now);

    // Handle a command (RPM, SPEED, BUTTON, etc.)
    void handleCommand(String* tokens, int count);

    // (Re)play the template's one-shot boot message sequence
    void startBootSequence();

    // Set a signal to a named state or (for numeric controls) a raw byte
    // value. Returns false if the template has no signal by that name.
    bool setSignal(const char* name, const char* value);

    // Get current state
    int getRPM() { return currentRPM; }
    int getSpeed() { return currentSpeed; }

private:
    MCP_CAN* canBus;
    Template* tmpl;

    // Current values
    int currentRPM;
    int currentSpeed;
    int currentGaugeValues[MAX_GAUGES];
    byte currentTempMessage[8];
    float currentTirePressures[MAX_TIRES];
    char currentVIN[18];
    unsigned char vinMessages[3][8];

    // Blinker state
    bool leftBlinkerActive;
    bool rightBlinkerActive;
    bool blinkerState;
    unsigned long lastBlinkerToggle;

    // Button state
    bool buttonActive;
    byte currentButtonData[8];
    unsigned long buttonPressStartTime;

    // Boot sequence state
    bool bootActive;
    byte bootIndex;
    unsigned long bootNextTime;

    // Runtime signal overrides (byte spans overlaid on outgoing frames).
    // Slot i corresponds to tmpl->signals[i].
    struct SignalOverride {
        bool used;
        unsigned long canId;
        byte startByte;
        byte data[8];
        byte len;
    };
    SignalOverride sigOverrides[MAX_SIGNALS];

    // Timing
    unsigned long lastButtonTime;
    unsigned long lastGaugeTimes[MAX_GAUGES];
    unsigned long lastTempTime;
    unsigned long lastTpmsTime;
    unsigned long lastVinTime;
    unsigned long lastBlinkerTime;
    unsigned long lastBackgroundTimes[MAX_BACKGROUND_MSGS];

    // Internal methods
    void sendFrame(unsigned long canId, byte ext, byte len, const byte* data);
    void applyOverrides(unsigned long canId, byte* data, byte len);
    void applySignalDefaults();
    void prepareVINMessages();
    void handleButtonCommand(const String& buttonName);
    void sendButtonMessage();
    void sendDefaultMessage();
    void sendGaugeMessage(int gaugeIndex);
    void sendTemperatureMessage();
    void sendTirePressureMessage();
    void sendVINMessages();
    void sendBlinkerMessages(unsigned long now);
    void sendBackgroundMessage(int index);
};

// Global runner instance
extern TemplateRunner templateRunner;
