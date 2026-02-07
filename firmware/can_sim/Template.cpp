#include "Template.h"
#include <string.h>

void initTemplate(Template* t) {
    memset(t, 0, sizeof(Template));
    strcpy(t->id, "blank");
    strcpy(t->name, "Blank Template");
    strcpy(t->version, "1.0.0");
    t->isOfficial = false;

    // Default button settings
    t->buttonCanId = 0x81;
    t->buttonHoldMs = 100;
    t->buttonIntervalMs = 10;

    // Default intervals
    t->tempIntervalMs = 100;
    t->tpmsIntervalMs = 200;
    t->blinkerIntervalMs = 10;
    t->blinkerBlinkRateMs = 500;
    t->vinIntervalMs = 200;
}

GaugeDef* findGauge(Template* t, const char* name) {
    for (int i = 0; i < t->numGauges; i++) {
        if (strcasecmp(t->gauges[i].name, name) == 0) {
            return &t->gauges[i];
        }
    }
    return nullptr;
}

ButtonDef* findButton(Template* t, const char* name) {
    for (int i = 0; i < t->numButtons; i++) {
        if (strcasecmp(t->buttons[i].name, name) == 0) {
            return &t->buttons[i];
        }
    }
    return nullptr;
}

TempSensorDef* findTempSensor(Template* t, const char* name) {
    for (int i = 0; i < t->numTempSensors; i++) {
        if (strcasecmp(t->tempSensors[i].name, name) == 0) {
            return &t->tempSensors[i];
        }
    }
    return nullptr;
}

TireDef* findTire(Template* t, const char* name) {
    for (int i = 0; i < t->numTires; i++) {
        if (strcasecmp(t->tires[i].name, name) == 0) {
            return &t->tires[i];
        }
    }
    return nullptr;
}
