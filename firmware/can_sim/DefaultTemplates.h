#pragma once

#include "Template.h"

// Number of built-in official templates
#define NUM_DEFAULT_TEMPLATES 2

// ---- Signal-building helpers (keep the templates readable) ----

static byte dtHexBytes(const char* s, byte* out) {
    byte n = 0;
    while (*s && n < 8) {
        while (*s == ' ') s++;
        if (!*s) break;
        char buf[3];
        buf[0] = s[0];
        buf[1] = (s[1] && s[1] != ' ') ? s[1] : '\0';
        buf[2] = '\0';
        out[n++] = (byte)strtol(buf, nullptr, 16);
        s++;
        if (*s && *s != ' ') s++;
    }
    return n;
}

static SignalDef* dtSignal(Template* t, const char* name, unsigned long canId, byte startByte) {
    SignalDef* s = &t->signals[t->numSignals++];
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->canId = canId;
    s->startByte = startByte;
    s->numStates = 0;
    s->defaultState = 0xFF;
    return s;
}

static void dtState(SignalDef* s, const char* name, const char* hex, bool isDefault) {
    if (s->numStates >= MAX_SIGNAL_STATES) return;
    SignalStateDef* st = &s->states[s->numStates];
    snprintf(st->name, sizeof(st->name), "%s", name);
    st->len = dtHexBytes(hex, st->data);
    if (isDefault) s->defaultState = s->numStates;
    s->numStates++;
}

// Initialize the Mustang S550 template
void initMustangS550Template(Template* t) {
    initTemplate(t);

    // Metadata
    strcpy(t->id, "mustang_s550");
    strcpy(t->name, "Ford Mustang S550");
    strcpy(t->version, "1.0.0");
    t->isOfficial = true;

    // Buttons - CAN ID 0x81
    t->buttonCanId = 0x81;
    t->buttonHoldMs = 100;
    t->buttonIntervalMs = 10;
    memset(t->buttonDefault, 0, 8);

    // Button definitions
    t->numButtons = 6;

    strcpy(t->buttons[0].name, "UP");
    t->buttons[0].data[0] = 0x08;

    strcpy(t->buttons[1].name, "DOWN");
    t->buttons[1].data[0] = 0x01;

    strcpy(t->buttons[2].name, "LEFT");
    t->buttons[2].data[0] = 0x02;

    strcpy(t->buttons[3].name, "RIGHT");
    t->buttons[3].data[0] = 0x04;

    strcpy(t->buttons[4].name, "OK");
    t->buttons[4].data[0] = 0x10;

    strcpy(t->buttons[5].name, "SETTINGS");
    t->buttons[5].data[0] = 0x46;
    t->buttons[5].data[1] = 0x01;

    // Gauges
    t->numGauges = 2;

    // RPM gauge
    strcpy(t->gauges[0].name, "RPM");
    t->gauges[0].canId = 0x204;
    memset(t->gauges[0].base, 0, 8);
    t->gauges[0].valueBytes[0] = 3;
    t->gauges[0].valueBytes[1] = 4;
    t->gauges[0].numValueBytes = 2;
    t->gauges[0].scale = 0.5f;
    t->gauges[0].offset = 0;
    t->gauges[0].minVal = 0;
    t->gauges[0].maxVal = 8000;
    t->gauges[0].intervalMs = 100;

    // Speed gauge
    strcpy(t->gauges[1].name, "SPEED");
    t->gauges[1].canId = 0x202;
    memset(t->gauges[1].base, 0, 8);
    t->gauges[1].base[4] = 0x60;
    t->gauges[1].valueBytes[0] = 6;
    t->gauges[1].valueBytes[1] = 7;
    t->gauges[1].numValueBytes = 2;
    t->gauges[1].scale = 159.0f;
    t->gauges[1].offset = 0;
    t->gauges[1].minVal = 0;
    t->gauges[1].maxVal = 200;
    t->gauges[1].intervalMs = 100;

    // Temperature
    t->tempCanId = 0x156;
    memset(t->tempBase, 0, 8);
    t->tempBase[4] = 0x03;
    t->tempIntervalMs = 100;
    t->numTempSensors = 2;

    strcpy(t->tempSensors[0].name, "COOLANT");
    t->tempSensors[0].byteIndex = 0;
    t->tempSensors[0].offset = 60;

    strcpy(t->tempSensors[1].name, "OIL");
    t->tempSensors[1].byteIndex = 1;
    t->tempSensors[1].offset = 60;

    // TPMS
    t->tpmsCanId = 0x3B5;
    memset(t->tpmsBase, 0, 8);
    t->tpmsIntervalMs = 200;
    t->numTires = 4;

    strcpy(t->tires[0].name, "FL");
    t->tires[0].byteIndex = 1;
    t->tires[0].scale = 6.895f;

    strcpy(t->tires[1].name, "FR");
    t->tires[1].byteIndex = 3;
    t->tires[1].scale = 6.895f;

    strcpy(t->tires[2].name, "RL");
    t->tires[2].byteIndex = 7;
    t->tires[2].scale = 6.895f;

    strcpy(t->tires[3].name, "RR");
    t->tires[3].byteIndex = 5;
    t->tires[3].scale = 6.895f;

    // Blinkers
    t->blinkerCanId = 0x3B3;
    t->blinkerCanIdAlt = 0x3B2;
    t->blinkerBase[0] = 0x40;
    t->blinkerBase[1] = 0x48;
    t->blinkerBase[2] = 0xC0;
    t->blinkerBase[3] = 0x10;
    t->blinkerBase[4] = 0x10;
    t->blinkerBase[5] = 0x00;
    t->blinkerBase[6] = 0x00;
    t->blinkerBase[7] = 0x02;
    t->blinkerIntervalMs = 10;
    t->blinkerBlinkRateMs = 500;
    t->blinker.leftByte = 6;
    t->blinker.leftMask = 0x40;
    t->blinker.rightByte = 4;
    t->blinker.rightMask = 0x08;

    // VIN
    t->vinCanId = 0x40A;
    t->vinIntervalMs = 200;
    t->vinFramePrefix = 0xC1;

    // Background messages
    t->numBackgroundMsgs = 2;

    // Cluster keepalive (0x109)
    t->backgroundMsgs[0].canId = 0x109;
    t->backgroundMsgs[0].data[0] = 0x00;
    t->backgroundMsgs[0].data[1] = 0x03;
    t->backgroundMsgs[0].data[2] = 0x01;
    t->backgroundMsgs[0].data[3] = 0x00;
    t->backgroundMsgs[0].data[4] = 0x00;
    t->backgroundMsgs[0].data[5] = 0x00;
    t->backgroundMsgs[0].data[6] = 0x00;
    t->backgroundMsgs[0].data[7] = 0x28;
    t->backgroundMsgs[0].len = 8;
    t->backgroundMsgs[0].intervalMs = 10;

    // ABS/Traction control (0x416)
    t->backgroundMsgs[1].canId = 0x416;
    t->backgroundMsgs[1].data[0] = 0x50;
    t->backgroundMsgs[1].data[1] = 0x00;
    t->backgroundMsgs[1].data[2] = 0xFE;
    t->backgroundMsgs[1].data[3] = 0x00;
    t->backgroundMsgs[1].data[4] = 0x01;
    t->backgroundMsgs[1].data[5] = 0x00;
    t->backgroundMsgs[1].data[6] = 0x00;
    t->backgroundMsgs[1].data[7] = 0x00;
    t->backgroundMsgs[1].len = 8;
    t->backgroundMsgs[1].intervalMs = 10;

    // Gear / reverse (0x171, Sync 4). Transmitted continuously so the GEAR
    // signal below can overlay it; REVERSE needs B0=0x36 & B1=0x32.
    t->backgroundMsgs[2].canId = 0x171;
    for (int i = 0; i < 8; i++) t->backgroundMsgs[2].data[i] = 0x00;
    t->backgroundMsgs[2].len = 8;
    t->backgroundMsgs[2].intervalMs = 10;

    t->numBackgroundMsgs = 3;

    // ---- Signals (named byte-span overrides on the frames above) ----
    t->numSignals = 0;

    // Gear selector over 0x171
    SignalDef* gear = dtSignal(t, "GEAR", 0x171, 0);
    dtState(gear, "PARK",    "00 00", true);
    dtState(gear, "REVERSE", "36 32", false);
    dtState(gear, "NEUTRAL", "00 00", false);
    dtState(gear, "DRIVE",   "00 00", false);

    // Lighting / body over the 0x3B3 (+0x3B2) frame
    // Headlight switch: B0 (headlamp) and B1 (DRL/night) move together on
    // this car, so they're one control written as a byte pair. Hazard is a
    // turn-signal function (B4/B6), handled by the Turn Signals control.
    SignalDef* head = dtSignal(t, "HEADLIGHTS", 0x3B3, 0);
    dtState(head, "DRL", "40 48", true);   // daytime running lights
    dtState(head, "ON",  "44 88", false);  // headlamps on (night)

    // Backlight is continuous (0x00-0x11); named states are shortcuts.
    // No default so the base brightness (0x10) is left untouched.
    SignalDef* bl = dtSignal(t, "BACKLIGHT", 0x3B3, 3);
    dtState(bl, "OFF",     "00", false);
    dtState(bl, "MYCOLOR", "0A", false);
    dtState(bl, "MAX",     "11", false);

    SignalDef* dn = dtSignal(t, "DAYNIGHT", 0x3B3, 5);
    dtState(dn, "DAY",   "00", true);
    dtState(dn, "NIGHT", "50", false);

    // Doors + hood share byte 7 (hood-closed 0x02 kept in door states)
    SignalDef* doors = dtSignal(t, "DOORS", 0x3B3, 7);
    dtState(doors, "CLOSED",    "02", true);
    dtState(doors, "PASSENGER", "12", false);
    dtState(doors, "DRIVER",    "22", false);
    dtState(doors, "BOTH",      "32", false);
    dtState(doors, "HOODOPEN",  "0A", false);

    // Warning lights over the 0x416 frame
    SignalDef* abs = dtSignal(t, "ABS", 0x416, 6);
    dtState(abs, "OFF",   "00", true);
    dtState(abs, "SOLID", "40", false);
    dtState(abs, "SLOW",  "80", false);
    dtState(abs, "FAST",  "D0", false);

    SignalDef* trac = dtSignal(t, "TRACTION", 0x416, 5);
    dtState(trac, "OFF",   "00", true);
    dtState(trac, "SOLID", "02", false);
    dtState(trac, "FLASH", "0F", false);
}

// Initialize the F-150 13th Gen template
void initF150Template(Template* t) {
    // Start with Mustang as base (very similar)
    initMustangS550Template(t);

    // Override metadata
    strcpy(t->id, "f150_13gen");
    strcpy(t->name, "Ford F-150 13th Gen");

    // F150 has different tire byte order
    t->tires[2].byteIndex = 5;  // RL
    t->tires[3].byteIndex = 7;  // RR

    // F150 has lower max RPM
    t->gauges[0].maxVal = 7000;

    // F150 has lower max speed
    t->gauges[1].maxVal = 120;
}

// Get a default template by index
void getDefaultTemplate(int index, Template* t) {
    switch (index) {
        case 0:
            initMustangS550Template(t);
            break;
        case 1:
            initF150Template(t);
            break;
        default:
            initTemplate(t);
            break;
    }
}

// Get a default template by ID
bool getDefaultTemplateById(const char* id, Template* t) {
    if (strcasecmp(id, "mustang_s550") == 0) {
        initMustangS550Template(t);
        return true;
    }
    if (strcasecmp(id, "f150_13gen") == 0) {
        initF150Template(t);
        return true;
    }
    return false;
}
