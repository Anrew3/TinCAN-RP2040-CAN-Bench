#include "TemplateRunner.h"
#include "Config.h"
#include <string.h>

// Global instance
TemplateRunner templateRunner;

TemplateRunner::TemplateRunner() {
    canBus = nullptr;
    tmpl = nullptr;

    currentRPM = 0;
    currentSpeed = 0;
    memset(currentGaugeValues, 0, sizeof(currentGaugeValues));
    memset(currentTempMessage, 0, sizeof(currentTempMessage));

    for (int i = 0; i < MAX_TIRES; i++) {
        currentTirePressures[i] = 35.0f;
    }

    strcpy(currentVIN, "10203040506070809");
    memset(vinMessages, 0, sizeof(vinMessages));

    leftBlinkerActive = false;
    rightBlinkerActive = false;
    blinkerState = false;
    lastBlinkerToggle = 0;

    buttonActive = false;
    memset(currentButtonData, 0, sizeof(currentButtonData));
    buttonPressStartTime = 0;

    lastButtonTime = 0;
    memset(lastGaugeTimes, 0, sizeof(lastGaugeTimes));
    lastTempTime = 0;
    lastTpmsTime = 0;
    lastVinTime = 0;
    lastBlinkerTime = 0;
    memset(lastBackgroundTimes, 0, sizeof(lastBackgroundTimes));
}

void TemplateRunner::begin(MCP_CAN* can) {
    canBus = can;
}

void TemplateRunner::setTemplate(Template* t) {
    tmpl = t;

    // Copy template's temp base to current temp message
    if (tmpl) {
        memcpy(currentTempMessage, tmpl->tempBase, 8);
        prepareVINMessages();
    }
}

void TemplateRunner::tick(unsigned long now) {
    if (!canBus || !tmpl) return;

    // Button/default message
    if (now - lastButtonTime >= tmpl->buttonIntervalMs) {
        if (buttonActive && (now - buttonPressStartTime <= tmpl->buttonHoldMs)) {
            sendButtonMessage();
        } else {
            buttonActive = false;
            sendDefaultMessage();
        }
        lastButtonTime = now;
    }

    // Gauge messages (RPM, Speed, etc.)
    for (int i = 0; i < tmpl->numGauges; i++) {
        if (now - lastGaugeTimes[i] >= tmpl->gauges[i].intervalMs) {
            sendGaugeMessage(i);
            lastGaugeTimes[i] = now;
        }
    }

    // Temperature message
    if (now - lastTempTime >= tmpl->tempIntervalMs) {
        sendTemperatureMessage();
        lastTempTime = now;
    }

    // TPMS message
    if (now - lastTpmsTime >= tmpl->tpmsIntervalMs) {
        sendTirePressureMessage();
        lastTpmsTime = now;
    }

    // VIN messages
    if (now - lastVinTime >= tmpl->vinIntervalMs) {
        sendVINMessages();
        lastVinTime = now;
    }

    // Blinker messages
    if (now - lastBlinkerTime >= tmpl->blinkerIntervalMs) {
        sendBlinkerMessages(now);
        lastBlinkerTime = now;
    }

    // Background messages
    for (int i = 0; i < tmpl->numBackgroundMsgs; i++) {
        if (now - lastBackgroundTimes[i] >= tmpl->backgroundMsgs[i].intervalMs) {
            sendBackgroundMessage(i);
            lastBackgroundTimes[i] = now;
        }
    }
}

void TemplateRunner::handleCommand(String* tokens, int count) {
    if (count <= 0 || !tmpl) return;

    String cmd = tokens[0];
    cmd.toUpperCase();

    // Check if it's a gauge command
    GaugeDef* gauge = findGauge(tmpl, cmd.c_str());
    if (gauge && count >= 2) {
        int value = tokens[1].toInt();

        // Find gauge index and store value
        for (int i = 0; i < tmpl->numGauges; i++) {
            if (strcasecmp(tmpl->gauges[i].name, cmd.c_str()) == 0) {
                currentGaugeValues[i] = value;

                // Special case: also update currentRPM/currentSpeed for legacy access
                if (strcasecmp(cmd.c_str(), "RPM") == 0) currentRPM = value;
                if (strcasecmp(cmd.c_str(), "SPEED") == 0) currentSpeed = value;

                if (Serial && !g_quietSet) {
                    Serial.print("[Runner] ");
                    Serial.print(gauge->name);
                    Serial.print(" set to ");
                    Serial.println(value);
                }
                return;
            }
        }
    }

    // Temperature command: TEMP:COOLANT:190
    if (cmd == "TEMP" && count >= 3) {
        String sensorName = tokens[1];
        int tempF = tokens[2].toInt();

        TempSensorDef* sensor = findTempSensor(tmpl, sensorName.c_str());
        if (sensor) {
            // Convert F to internal value: (F - 32) * 5 / 9 + offset
            int tempC = (tempF - 32) * 5 / 9;
            int tempHex = tempC + sensor->offset;
            currentTempMessage[sensor->byteIndex] = (byte)tempHex;

            if (Serial) {
                Serial.print("[Runner] ");
                Serial.print(sensorName);
                Serial.print(" temp set to ");
                Serial.print(tempF);
                Serial.println("F");
            }
        } else {
            if (Serial) Serial.println("[Runner] Unknown temp sensor");
        }
        return;
    }

    // TPMS command: TPMS:FL:35.0
    if (cmd == "TPMS" && count >= 3) {
        String tireName = tokens[1];
        float pressure = tokens[2].toFloat();

        for (int i = 0; i < tmpl->numTires; i++) {
            if (strcasecmp(tmpl->tires[i].name, tireName.c_str()) == 0) {
                currentTirePressures[i] = pressure;
                if (Serial) {
                    Serial.print("[Runner] ");
                    Serial.print(tireName);
                    Serial.print(" pressure set to ");
                    Serial.print(pressure);
                    Serial.println(" PSI");
                }
                return;
            }
        }
        if (Serial) Serial.println("[Runner] Unknown tire");
        return;
    }

    // VIN command: VIN:1FAFP40486F123456
    if (cmd == "VIN" && count >= 2) {
        String newVIN = tokens[1];
        if (newVIN.length() != 17) {
            if (Serial) Serial.println("[Runner] Invalid VIN length (must be 17)");
            return;
        }
        newVIN.toCharArray(currentVIN, 18);
        prepareVINMessages();
        if (Serial) {
            Serial.print("[Runner] VIN set to: ");
            Serial.println(currentVIN);
        }
        return;
    }

    // Blinker command: BLINKER:LEFT/RIGHT/BOTH/OFF
    if (cmd == "BLINKER" && count >= 2) {
        String which = tokens[1];
        which.toUpperCase();

        if (which == "LEFT") {
            leftBlinkerActive = true;
            rightBlinkerActive = false;
        } else if (which == "RIGHT") {
            leftBlinkerActive = false;
            rightBlinkerActive = true;
        } else if (which == "BOTH" || which == "HAZARD") {
            leftBlinkerActive = true;
            rightBlinkerActive = true;
        } else if (which == "OFF") {
            leftBlinkerActive = false;
            rightBlinkerActive = false;
        }

        if (Serial) {
            Serial.print("[Runner] Blinker: ");
            Serial.println(which);
        }
        return;
    }

    // Hazards toggle
    if (cmd == "HAZARDS") {
        leftBlinkerActive = !leftBlinkerActive;
        rightBlinkerActive = leftBlinkerActive;
        if (Serial) {
            Serial.print("[Runner] Hazards ");
            Serial.println(leftBlinkerActive ? "ON" : "OFF");
        }
        return;
    }

    // Button commands: UP, DOWN, LEFT, RIGHT, OK, SETTINGS
    ButtonDef* button = findButton(tmpl, cmd.c_str());
    if (button) {
        handleButtonCommand(cmd);
        return;
    }

    // Unknown command
    if (Serial) {
        Serial.print("[Runner] Unknown command: ");
        Serial.println(cmd);
    }
}

// Internal methods

void TemplateRunner::prepareVINMessages() {
    if (!tmpl) return;

    memset(vinMessages, 0, sizeof(vinMessages));

    for (int i = 0; i < 17; i++) {
        int msgIndex = i / 6;
        int byteIndex = (i % 6) + 2;

        vinMessages[msgIndex][0] = tmpl->vinFramePrefix;
        vinMessages[msgIndex][1] = (unsigned char)msgIndex;
        vinMessages[msgIndex][byteIndex] = (unsigned char)currentVIN[i];
    }

    // Pad last message
    vinMessages[2][7] = 0xFF;
}

void TemplateRunner::handleButtonCommand(const String& buttonName) {
    if (!tmpl) return;

    ButtonDef* button = findButton(tmpl, buttonName.c_str());
    if (!button) return;

    memcpy(currentButtonData, button->data, 8);
    buttonPressStartTime = millis();
    buttonActive = true;

    if (Serial && g_verboseSerial) {
        Serial.print("[Runner] Button press: ");
        Serial.println(buttonName);
    }
}

void TemplateRunner::sendButtonMessage() {
    if (!canBus || !tmpl) return;

    byte status = canBus->sendMsgBuf(tmpl->buttonCanId, 0, 8, currentButtonData);

    if (Serial && g_verboseSerial && canLog(millis())) {
        Serial.print("[Runner] Button msg sent, status=");
        Serial.println(status == CAN_OK ? "OK" : "ERR");
    }
}

void TemplateRunner::sendDefaultMessage() {
    if (!canBus || !tmpl) return;

    canBus->sendMsgBuf(tmpl->buttonCanId, 0, 8, tmpl->buttonDefault);
}

void TemplateRunner::sendGaugeMessage(int gaugeIndex) {
    if (!canBus || !tmpl || gaugeIndex >= tmpl->numGauges) return;

    GaugeDef* gauge = &tmpl->gauges[gaugeIndex];
    int value = currentGaugeValues[gaugeIndex];

    // Apply scaling
    int scaledValue = (int)(value * gauge->scale) + gauge->offset;

    // Prepare message from base
    byte msg[8];
    memcpy(msg, gauge->base, 8);

    // Insert value into appropriate bytes
    if (gauge->numValueBytes == 2) {
        msg[gauge->valueBytes[0]] = (scaledValue >> 8) & 0xFF;
        msg[gauge->valueBytes[1]] = scaledValue & 0xFF;
    } else if (gauge->numValueBytes == 1) {
        msg[gauge->valueBytes[0]] = scaledValue & 0xFF;
    }

    byte status = canBus->sendMsgBuf(gauge->canId, 0, 8, msg);

    if (Serial && g_verboseSerial && canLog(millis())) {
        Serial.print("[Runner] ");
        Serial.print(gauge->name);
        Serial.print(" msg sent, status=");
        Serial.println(status == CAN_OK ? "OK" : "ERR");
    }
}

void TemplateRunner::sendTemperatureMessage() {
    if (!canBus || !tmpl) return;

    canBus->sendMsgBuf(tmpl->tempCanId, 0, 8, currentTempMessage);
}

void TemplateRunner::sendTirePressureMessage() {
    if (!canBus || !tmpl) return;

    byte msg[8];
    memcpy(msg, tmpl->tpmsBase, 8);

    for (int i = 0; i < tmpl->numTires; i++) {
        float psi = currentTirePressures[i];
        int kpa = (int)(psi * tmpl->tires[i].scale + 0.5f);
        msg[tmpl->tires[i].byteIndex] = (byte)(kpa & 0xFF);
    }

    canBus->sendMsgBuf(tmpl->tpmsCanId, 0, 8, msg);
}

void TemplateRunner::sendVINMessages() {
    if (!canBus || !tmpl) return;

    for (int i = 0; i < 3; i++) {
        canBus->sendMsgBuf(tmpl->vinCanId, 0, 8, vinMessages[i]);
    }
}

void TemplateRunner::sendBlinkerMessages(unsigned long now) {
    if (!canBus || !tmpl) return;

    // Toggle blinker state
    if (now - lastBlinkerToggle >= tmpl->blinkerBlinkRateMs) {
        blinkerState = !blinkerState;
        lastBlinkerToggle = now;
    }

    byte msg[8];
    memcpy(msg, tmpl->blinkerBase, 8);

    // Clear blinker bits first
    msg[tmpl->blinker.leftByte] &= ~tmpl->blinker.leftMask;
    msg[tmpl->blinker.rightByte] &= ~tmpl->blinker.rightMask;

    // Set based on state
    if (blinkerState) {
        if (leftBlinkerActive) {
            msg[tmpl->blinker.leftByte] |= tmpl->blinker.leftMask;
        }
        if (rightBlinkerActive) {
            msg[tmpl->blinker.rightByte] |= tmpl->blinker.rightMask;
        }
    }

    canBus->sendMsgBuf(tmpl->blinkerCanId, 0, 8, msg);

    if (tmpl->blinkerCanIdAlt != 0 && tmpl->blinkerCanIdAlt != tmpl->blinkerCanId) {
        canBus->sendMsgBuf(tmpl->blinkerCanIdAlt, 0, 8, msg);
    }
}

void TemplateRunner::sendBackgroundMessage(int index) {
    if (!canBus || !tmpl || index >= tmpl->numBackgroundMsgs) return;

    BackgroundMsgDef* bg = &tmpl->backgroundMsgs[index];
    canBus->sendMsgBuf(bg->canId, 0, bg->len, bg->data);
}
