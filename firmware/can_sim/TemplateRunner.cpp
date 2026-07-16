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
    bodyWakeMode = false;

    buttonActive = false;
    memset(currentButtonData, 0, sizeof(currentButtonData));
    buttonPressStartTime = 0;

    bootActive = false;
    bootIndex = 0;
    bootNextTime = 0;

    for (int i = 0; i < MAX_SIGNALS; i++) sigOverrides[i].used = false;

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
        applySignalDefaults();
        startBootSequence();
        // Start in wake mode if the template defines a wake payload
        bodyWakeMode = tmpl->blinkerHasWake && tmpl->blinkerWakeDefault;
    }
}

void TemplateRunner::setBodyWakeMode(bool wake) {
    bodyWakeMode = wake;
    if (Serial) {
        Serial.print("[Body] 0x3B3 mode = ");
        Serial.println(wake ? "WAKE" : "LIGHT");
    }
}

// ---- Signal / byte-override engine ----

void TemplateRunner::applySignalDefaults() {
    for (int i = 0; i < MAX_SIGNALS; i++) sigOverrides[i].used = false;
    if (!tmpl) return;

    for (int i = 0; i < tmpl->numSignals && i < MAX_SIGNALS; i++) {
        SignalDef* s = &tmpl->signals[i];
        if (s->defaultState == 0xFF || s->defaultState >= s->numStates) continue;
        SignalStateDef* st = &s->states[s->defaultState];
        sigOverrides[i].used = true;
        sigOverrides[i].canId = s->canId;
        sigOverrides[i].startByte = s->startByte;
        sigOverrides[i].len = st->len;
        memcpy(sigOverrides[i].data, st->data, 8);
    }
}

void TemplateRunner::applyOverrides(unsigned long canId, byte* data, byte len) {
    for (int i = 0; i < MAX_SIGNALS; i++) {
        SignalOverride* o = &sigOverrides[i];
        if (!o->used || o->canId != canId) continue;
        for (int b = 0; b < o->len; b++) {
            byte idx = o->startByte + b;
            if (idx < len) data[idx] = o->data[b];
        }
    }
}

void TemplateRunner::sendFrame(unsigned long canId, byte ext, byte len, const byte* data) {
    if (!canBus) return;
    byte buf[8];
    byte n = len > 8 ? 8 : len;
    memcpy(buf, data, n);
    applyOverrides(canId, buf, n);
    canBus->sendMsgBuf(canId, ext, n, buf);
}

bool TemplateRunner::setSignal(const char* name, const char* value) {
    if (!tmpl) return false;

    int sigIndex = -1;
    for (int i = 0; i < tmpl->numSignals && i < MAX_SIGNALS; i++) {
        if (strcasecmp(tmpl->signals[i].name, name) == 0) { sigIndex = i; break; }
    }
    if (sigIndex < 0) return false;

    SignalDef* s = &tmpl->signals[sigIndex];
    SignalOverride* o = &sigOverrides[sigIndex];
    o->canId = s->canId;
    o->startByte = s->startByte;

    // Named state?
    for (int i = 0; i < s->numStates; i++) {
        if (strcasecmp(s->states[i].name, value) == 0) {
            o->len = s->states[i].len;
            memcpy(o->data, s->states[i].data, 8);
            o->used = true;
            if (Serial && !g_quietSet) {
                Serial.print("[Signal] ");
                Serial.print(s->name);
                Serial.print(" = ");
                Serial.println(s->states[i].name);
            }
            return true;
        }
    }

    // Otherwise treat as a raw numeric byte (e.g. backlight level)
    o->len = 1;
    o->data[0] = (byte)(strtol(value, nullptr, 0) & 0xFF);
    o->used = true;
    if (Serial && !g_quietSet) {
        Serial.print("[Signal] ");
        Serial.print(s->name);
        Serial.print(" = 0x");
        Serial.println(o->data[0], HEX);
    }
    return true;
}

void TemplateRunner::startBootSequence() {
    bootIndex = 0;
    bootActive = (tmpl && tmpl->numBootMsgs > 0);
    if (bootActive) {
        bootNextTime = millis() + tmpl->bootMsgs[0].delayMs;
    }
}

void TemplateRunner::tick(unsigned long now) {
    if (!canBus || !tmpl) return;

    // One-shot boot sequence, paced by each message's delayMs
    while (bootActive && (long)(now - bootNextTime) >= 0) {
        BootMsgDef* m = &tmpl->bootMsgs[bootIndex];
        canBus->sendMsgBuf(m->canId, 0, m->len, m->data);
        bootIndex++;
        if (bootIndex >= tmpl->numBootMsgs) {
            bootActive = false;
        } else {
            bootNextTime = now + tmpl->bootMsgs[bootIndex].delayMs;
        }
    }

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

    // SIGNAL:<name>:<state|value> - set a template-defined signal
    if (cmd == "SIGNAL" && count >= 3) {
        if (!setSignal(tokens[1].c_str(), tokens[2].c_str())) {
            if (Serial) {
                Serial.print("[Runner] Unknown signal: ");
                Serial.println(tokens[1]);
            }
        }
        return;
    }

    // GEAR:<state> - shorthand for SIGNAL:GEAR:<state>
    if (cmd == "GEAR" && count >= 2) {
        if (!setSignal("GEAR", tokens[1].c_str())) {
            if (Serial) Serial.println("[Runner] No GEAR signal in this template");
        }
        return;
    }

    // BODY:WAKE|LIGHT - switch the 0x3B3 frame between the SYNC wake payload
    // and the lighting base
    if (cmd == "BODY" && count >= 2) {
        String m = tokens[1];
        m.toUpperCase();
        if (m == "WAKE") setBodyWakeMode(true);
        else if (m == "LIGHT" || m == "LIGHTING") setBodyWakeMode(false);
        else if (Serial) Serial.println("Usage: BODY:WAKE|LIGHT");
        return;
    }

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

    sendFrame(tmpl->buttonCanId, 0, 8, currentButtonData);

    if (Serial && g_verboseSerial && canLog(millis())) {
        Serial.println("[Runner] Button msg sent");
    }
}

void TemplateRunner::sendDefaultMessage() {
    if (!canBus || !tmpl) return;

    sendFrame(tmpl->buttonCanId, 0, 8, tmpl->buttonDefault);
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

    sendFrame(gauge->canId, 0, 8, msg);

    if (Serial && g_verboseSerial && canLog(millis())) {
        Serial.print("[Runner] ");
        Serial.print(gauge->name);
        Serial.println(" msg sent");
    }
}

void TemplateRunner::sendTemperatureMessage() {
    if (!canBus || !tmpl) return;

    sendFrame(tmpl->tempCanId, 0, 8, currentTempMessage);
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

    sendFrame(tmpl->tpmsCanId, 0, 8, msg);
}

void TemplateRunner::sendVINMessages() {
    if (!canBus || !tmpl) return;

    for (int i = 0; i < 3; i++) {
        sendFrame(tmpl->vinCanId, 0, 8, vinMessages[i]);
    }
}

void TemplateRunner::sendBlinkerMessages(unsigned long now) {
    if (!canBus || !tmpl) return;

    // WAKE mode: send the raw wake payload on 0x3B3 only (matches the v1
    // boot sketch that turns dormant SYNC head units on). No blinker bits,
    // no signal overlays, no 0x3B2 mirror - the module wants this exact frame.
    if (bodyWakeMode && tmpl->blinkerHasWake) {
        byte wake[8];
        memcpy(wake, tmpl->blinkerWakeBase, 8);
        canBus->sendMsgBuf(tmpl->blinkerCanId, 0, 8, wake);
        return;
    }

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

    // 0x3B2/0x3B3 carry the same lighting/body payload; apply signal
    // overrides for both IDs so the mirrors stay identical, then send once
    // to each (raw, since overrides are already baked into msg).
    applyOverrides(tmpl->blinkerCanId, msg, 8);
    if (tmpl->blinkerCanIdAlt != 0 && tmpl->blinkerCanIdAlt != tmpl->blinkerCanId) {
        applyOverrides(tmpl->blinkerCanIdAlt, msg, 8);
    }

    canBus->sendMsgBuf(tmpl->blinkerCanId, 0, 8, msg);

    if (tmpl->blinkerCanIdAlt != 0 && tmpl->blinkerCanIdAlt != tmpl->blinkerCanId) {
        canBus->sendMsgBuf(tmpl->blinkerCanIdAlt, 0, 8, msg);
    }
}

void TemplateRunner::sendBackgroundMessage(int index) {
    if (!canBus || !tmpl || index >= tmpl->numBackgroundMsgs) return;

    BackgroundMsgDef* bg = &tmpl->backgroundMsgs[index];
    sendFrame(bg->canId, 0, bg->len, bg->data);
}
