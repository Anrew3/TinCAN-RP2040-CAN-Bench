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

    // Timing
    unsigned long lastButtonTime;
    unsigned long lastGaugeTimes[MAX_GAUGES];
    unsigned long lastTempTime;
    unsigned long lastTpmsTime;
    unsigned long lastVinTime;
    unsigned long lastBlinkerTime;
    unsigned long lastBackgroundTimes[MAX_BACKGROUND_MSGS];

    // Internal methods
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
