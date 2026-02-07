#include "TemplateManager.h"
#include "DefaultTemplates.h"
#include "Config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

// Forward declarations for helper functions
static int parseHexBytes(const char* str, byte* out, int maxBytes);
static void bytesToHexString(const byte* data, int len, char* out, size_t outSize);

// Global instance
TemplateManager templateManager;

// File paths
#define TEMPLATES_DIR "/templates"
#define CURRENT_TEMPLATE_FILE "/current_template.txt"

TemplateManager::TemplateManager() {
    initialized = false;
    strcpy(currentTemplateId, "mustang_s550");
}

bool TemplateManager::begin() {
    // Initialize LittleFS
    if (!LittleFS.begin()) {
        // Try to format if mount fails
        if (Serial) Serial.println("[TemplateManager] Formatting LittleFS...");
        if (!LittleFS.format()) {
            if (Serial) Serial.println("[TemplateManager] LittleFS format failed!");
            return false;
        }
        if (!LittleFS.begin()) {
            if (Serial) Serial.println("[TemplateManager] LittleFS mount failed!");
            return false;
        }
    }

    // Create templates directory if it doesn't exist
    if (!LittleFS.exists(TEMPLATES_DIR)) {
        LittleFS.mkdir(TEMPLATES_DIR);
    }

    // Load the previously selected template ID
    loadCurrentTemplateId();

    // Load the current template
    if (!loadTemplate(currentTemplateId)) {
        // Fall back to default if load fails
        if (Serial) Serial.println("[TemplateManager] Loading default template");
        initMustangS550Template(&currentTemplate);
        strcpy(currentTemplateId, "mustang_s550");
    }

    initialized = true;

    if (Serial) {
        Serial.print("[TemplateManager] Initialized. Current template: ");
        Serial.println(currentTemplate.name);
    }

    return true;
}

Template* TemplateManager::getCurrentTemplate() {
    return &currentTemplate;
}

const char* TemplateManager::getCurrentTemplateId() {
    return currentTemplateId;
}

bool TemplateManager::loadTemplate(const char* id) {
    // First check if it's a built-in template
    if (getDefaultTemplateById(id, &currentTemplate)) {
        strcpy(currentTemplateId, id);
        saveCurrentTemplateId();
        if (Serial) {
            Serial.print("[TemplateManager] Loaded official template: ");
            Serial.println(currentTemplate.name);
        }
        return true;
    }

    // Check for custom template in LittleFS
    char filename[64];
    getTemplateFilename(id, filename, sizeof(filename));

    if (!LittleFS.exists(filename)) {
        if (Serial) {
            Serial.print("[TemplateManager] Template not found: ");
            Serial.println(id);
        }
        return false;
    }

    File file = LittleFS.open(filename, "r");
    if (!file) {
        if (Serial) Serial.println("[TemplateManager] Failed to open template file");
        return false;
    }

    size_t size = file.size();
    if (size > MAX_TEMPLATE_SIZE) {
        file.close();
        if (Serial) Serial.println("[TemplateManager] Template file too large");
        return false;
    }

    char* buffer = (char*)malloc(size + 1);
    if (!buffer) {
        file.close();
        if (Serial) Serial.println("[TemplateManager] Memory allocation failed");
        return false;
    }

    file.readBytes(buffer, size);
    buffer[size] = '\0';
    file.close();

    bool success = parseJsonToTemplate(buffer, size, &currentTemplate);
    free(buffer);

    if (success) {
        strcpy(currentTemplateId, id);
        saveCurrentTemplateId();
        if (Serial) {
            Serial.print("[TemplateManager] Loaded custom template: ");
            Serial.println(currentTemplate.name);
        }
    }

    return success;
}

bool TemplateManager::saveTemplate(const Template* t) {
    // Cannot save official templates
    if (t->isOfficial) {
        if (Serial) Serial.println("[TemplateManager] Cannot overwrite official template");
        return false;
    }

    char filename[64];
    getTemplateFilename(t->id, filename, sizeof(filename));

    // Convert template to JSON
    char* buffer = (char*)malloc(MAX_TEMPLATE_SIZE);
    if (!buffer) {
        if (Serial) Serial.println("[TemplateManager] Memory allocation failed");
        return false;
    }

    templateToJson(t, buffer, MAX_TEMPLATE_SIZE);

    File file = LittleFS.open(filename, "w");
    if (!file) {
        free(buffer);
        if (Serial) Serial.println("[TemplateManager] Failed to create template file");
        return false;
    }

    size_t written = file.print(buffer);
    file.close();
    free(buffer);

    if (written == 0) {
        if (Serial) Serial.println("[TemplateManager] Failed to write template");
        return false;
    }

    if (Serial) {
        Serial.print("[TemplateManager] Saved template: ");
        Serial.println(t->name);
    }

    return true;
}

bool TemplateManager::deleteTemplate(const char* id) {
    // Cannot delete official templates
    if (isOfficialTemplate(id)) {
        if (Serial) Serial.println("[TemplateManager] Cannot delete official template");
        return false;
    }

    char filename[64];
    getTemplateFilename(id, filename, sizeof(filename));

    if (!LittleFS.exists(filename)) {
        if (Serial) Serial.println("[TemplateManager] Template not found");
        return false;
    }

    if (LittleFS.remove(filename)) {
        if (Serial) {
            Serial.print("[TemplateManager] Deleted template: ");
            Serial.println(id);
        }

        // If we deleted the current template, switch to default
        if (strcmp(currentTemplateId, id) == 0) {
            loadTemplate("mustang_s550");
        }
        return true;
    }

    return false;
}

void TemplateManager::listTemplates() {
    if (Serial) {
        Serial.println("Available templates:");

        // List official templates
        Serial.println("  [Official]");
        for (int i = 0; i < NUM_DEFAULT_TEMPLATES; i++) {
            Template t;
            getDefaultTemplate(i, &t);
            Serial.print("    ");
            if (strcmp(t.id, currentTemplateId) == 0) Serial.print("* ");
            Serial.print(t.id);
            Serial.print(" - ");
            Serial.println(t.name);
        }

        // List custom templates
        Serial.println("  [Custom]");
        File dir = LittleFS.open(TEMPLATES_DIR, "r");
        bool hasCustom = false;
        if (dir && dir.isDirectory()) {
            File file = dir.openNextFile();
            while (file) {
                hasCustom = true;
                String filename = file.name();
                if (filename.endsWith(".json")) {
                    // Extract just the filename without path
                    int lastSlash = filename.lastIndexOf('/');
                    if (lastSlash >= 0) {
                        filename = filename.substring(lastSlash + 1);
                    }
                    String id = filename.substring(0, filename.length() - 5);
                    Serial.print("    ");
                    if (id == currentTemplateId) Serial.print("* ");
                    Serial.println(id);
                }
                file.close();
                file = dir.openNextFile();
            }
            dir.close();
        }
        if (!hasCustom) {
            Serial.println("    (none)");
        }
    }
}

bool TemplateManager::templateExists(const char* id) {
    // Check official templates
    if (isOfficialTemplate(id)) return true;

    // Check custom templates
    char filename[64];
    getTemplateFilename(id, filename, sizeof(filename));
    return LittleFS.exists(filename);
}

bool TemplateManager::isOfficialTemplate(const char* id) {
    return (strcasecmp(id, "mustang_s550") == 0 ||
            strcasecmp(id, "f150_13gen") == 0);
}

void TemplateManager::exportCurrentTemplate() {
    char* buffer = (char*)malloc(MAX_TEMPLATE_SIZE);
    if (!buffer) {
        if (Serial) Serial.println("[TemplateManager] Memory allocation failed");
        return;
    }

    templateToJson(&currentTemplate, buffer, MAX_TEMPLATE_SIZE);

    if (Serial) {
        Serial.println("TEMPLATE_JSON_START");
        Serial.println(buffer);
        Serial.println("TEMPLATE_JSON_END");
    }

    free(buffer);
}

bool TemplateManager::importTemplateFromJson(const char* json, size_t len) {
    Template t;
    if (!parseJsonToTemplate(json, len, &t)) {
        return false;
    }

    // Mark as not official (user uploaded)
    t.isOfficial = false;

    // Save to storage
    if (!saveTemplate(&t)) {
        return false;
    }

    // Switch to the new template
    return loadTemplate(t.id);
}

int TemplateManager::getTemplateCount() {
    int count = NUM_DEFAULT_TEMPLATES;

    File dir = LittleFS.open(TEMPLATES_DIR, "r");
    if (dir && dir.isDirectory()) {
        File file = dir.openNextFile();
        while (file) {
            String filename = file.name();
            if (filename.endsWith(".json")) {
                count++;
            }
            file.close();
            file = dir.openNextFile();
        }
        dir.close();
    }

    return count;
}

void TemplateManager::factoryReset() {
    // Delete all custom templates
    File dir = LittleFS.open(TEMPLATES_DIR, "r");
    if (dir && dir.isDirectory()) {
        File file = dir.openNextFile();
        while (file) {
            String filename = file.name();
            file.close();
            // Build full path and remove
            String path = String(TEMPLATES_DIR) + "/" + filename;
            // Handle case where name() returns full path
            if (filename.startsWith(TEMPLATES_DIR)) {
                path = filename;
            }
            LittleFS.remove(path);
            file = dir.openNextFile();
        }
        dir.close();
    }

    // Remove current template file
    LittleFS.remove(CURRENT_TEMPLATE_FILE);

    // Load default template
    initMustangS550Template(&currentTemplate);
    strcpy(currentTemplateId, "mustang_s550");

    if (Serial) Serial.println("[TemplateManager] Factory reset complete");
}

void TemplateManager::saveCurrentTemplateId() {
    File file = LittleFS.open(CURRENT_TEMPLATE_FILE, "w");
    if (file) {
        file.print(currentTemplateId);
        file.close();
    }
}

void TemplateManager::loadCurrentTemplateId() {
    if (LittleFS.exists(CURRENT_TEMPLATE_FILE)) {
        File file = LittleFS.open(CURRENT_TEMPLATE_FILE, "r");
        if (file) {
            String id = file.readString();
            id.trim();
            if (id.length() > 0 && id.length() < 32) {
                strcpy(currentTemplateId, id.c_str());
            }
            file.close();
        }
    }
}

void TemplateManager::getTemplateFilename(const char* id, char* filename, size_t len) {
    snprintf(filename, len, "%s/%s.json", TEMPLATES_DIR, id);
}

bool TemplateManager::parseJsonToTemplate(const char* json, size_t len, Template* t) {
    // Use ArduinoJson to parse
    StaticJsonDocument<4096> doc;

    DeserializationError error = deserializeJson(doc, json, len);
    if (error) {
        if (Serial) {
            Serial.print("[TemplateManager] JSON parse error: ");
            Serial.println(error.c_str());
        }
        return false;
    }

    initTemplate(t);

    // Metadata
    if (doc.containsKey("id")) {
        strlcpy(t->id, doc["id"] | "custom", sizeof(t->id));
    }
    if (doc.containsKey("name")) {
        strlcpy(t->name, doc["name"] | "Custom Template", sizeof(t->name));
    }
    if (doc.containsKey("version")) {
        strlcpy(t->version, doc["version"] | "1.0.0", sizeof(t->version));
    }

    // Buttons
    if (doc.containsKey("buttons")) {
        JsonObject buttons = doc["buttons"];
        t->buttonCanId = strtoul(buttons["canId"] | "0x81", nullptr, 0);
        t->buttonHoldMs = buttons["holdMs"] | 100;
        t->buttonIntervalMs = buttons["intervalMs"] | 10;

        // Parse default bytes
        if (buttons.containsKey("default")) {
            String defStr = buttons["default"].as<String>();
            parseHexBytes(defStr.c_str(), t->buttonDefault, 8);
        }

        // Parse button commands
        if (buttons.containsKey("commands")) {
            JsonObject cmds = buttons["commands"];
            t->numButtons = 0;
            for (JsonPair kv : cmds) {
                if (t->numButtons >= MAX_BUTTONS) break;
                strlcpy(t->buttons[t->numButtons].name, kv.key().c_str(), 16);
                String dataStr = kv.value().as<String>();
                memset(t->buttons[t->numButtons].data, 0, 8);
                parseHexBytes(dataStr.c_str(), t->buttons[t->numButtons].data, 8);
                t->numButtons++;
            }
        }
    }

    // Gauges
    if (doc.containsKey("gauges")) {
        JsonObject gauges = doc["gauges"];
        t->numGauges = 0;
        for (JsonPair kv : gauges) {
            if (t->numGauges >= MAX_GAUGES) break;
            GaugeDef* g = &t->gauges[t->numGauges];

            strlcpy(g->name, kv.key().c_str(), 16);
            JsonObject gobj = kv.value();

            g->canId = strtoul(gobj["canId"] | "0x000", nullptr, 0);

            if (gobj.containsKey("base")) {
                String baseStr = gobj["base"].as<String>();
                parseHexBytes(baseStr.c_str(), g->base, 8);
            }

            JsonArray vb = gobj["valueBytes"];
            g->numValueBytes = 0;
            for (int i = 0; i < 2 && i < vb.size(); i++) {
                g->valueBytes[i] = vb[i];
                g->numValueBytes++;
            }

            g->scale = gobj["scale"] | 1.0f;
            g->offset = gobj["offset"] | 0;
            g->minVal = gobj["min"] | 0;
            g->maxVal = gobj["max"] | 100;
            g->intervalMs = gobj["intervalMs"] | 100;

            t->numGauges++;
        }
    }

    // Temperature
    if (doc.containsKey("temperature")) {
        JsonObject temp = doc["temperature"];
        t->tempCanId = strtoul(temp["canId"] | "0x156", nullptr, 0);
        t->tempIntervalMs = temp["intervalMs"] | 100;

        if (temp.containsKey("base")) {
            String baseStr = temp["base"].as<String>();
            parseHexBytes(baseStr.c_str(), t->tempBase, 8);
        }

        if (temp.containsKey("sensors")) {
            JsonObject sensors = temp["sensors"];
            t->numTempSensors = 0;
            for (JsonPair kv : sensors) {
                if (t->numTempSensors >= MAX_TEMP_SENSORS) break;
                TempSensorDef* s = &t->tempSensors[t->numTempSensors];

                strlcpy(s->name, kv.key().c_str(), 16);
                JsonObject sobj = kv.value();
                s->byteIndex = sobj["byte"] | 0;
                s->offset = 60; // Default Ford offset

                t->numTempSensors++;
            }
        }
    }

    // TPMS
    if (doc.containsKey("tpms")) {
        JsonObject tpms = doc["tpms"];
        t->tpmsCanId = strtoul(tpms["canId"] | "0x3B5", nullptr, 0);
        t->tpmsIntervalMs = tpms["intervalMs"] | 200;

        if (tpms.containsKey("base")) {
            String baseStr = tpms["base"].as<String>();
            parseHexBytes(baseStr.c_str(), t->tpmsBase, 8);
        }

        if (tpms.containsKey("tires")) {
            JsonObject tires = tpms["tires"];
            t->numTires = 0;
            for (JsonPair kv : tires) {
                if (t->numTires >= MAX_TIRES) break;
                TireDef* tire = &t->tires[t->numTires];

                strlcpy(tire->name, kv.key().c_str(), 8);
                JsonObject tobj = kv.value();
                tire->byteIndex = tobj["byte"] | 0;
                tire->scale = tobj["scale"] | 6.895f;

                t->numTires++;
            }
        }
    }

    // Blinkers
    if (doc.containsKey("blinkers")) {
        JsonObject blinkers = doc["blinkers"];
        t->blinkerCanId = strtoul(blinkers["canId"] | "0x3B3", nullptr, 0);
        t->blinkerCanIdAlt = strtoul(blinkers["canIdAlt"] | "0x3B2", nullptr, 0);
        t->blinkerIntervalMs = blinkers["intervalMs"] | 10;
        t->blinkerBlinkRateMs = blinkers["blinkRateMs"] | 500;

        if (blinkers.containsKey("base")) {
            String baseStr = blinkers["base"].as<String>();
            parseHexBytes(baseStr.c_str(), t->blinkerBase, 8);
        }

        if (blinkers.containsKey("left")) {
            JsonObject left = blinkers["left"];
            t->blinker.leftByte = left["byte"] | 0;
            t->blinker.leftMask = strtoul(left["mask"] | "0x00", nullptr, 0);
        }
        if (blinkers.containsKey("right")) {
            JsonObject right = blinkers["right"];
            t->blinker.rightByte = right["byte"] | 0;
            t->blinker.rightMask = strtoul(right["mask"] | "0x00", nullptr, 0);
        }
    }

    // VIN
    if (doc.containsKey("vin")) {
        JsonObject vin = doc["vin"];
        t->vinCanId = strtoul(vin["canId"] | "0x40A", nullptr, 0);
        t->vinIntervalMs = vin["intervalMs"] | 200;

        String prefix = vin["framePrefix"] | "C1";
        t->vinFramePrefix = strtoul(prefix.c_str(), nullptr, 16);
    }

    // Background messages
    if (doc.containsKey("background")) {
        JsonArray bg = doc["background"];
        t->numBackgroundMsgs = 0;
        for (JsonObject msg : bg) {
            if (t->numBackgroundMsgs >= MAX_BACKGROUND_MSGS) break;
            BackgroundMsgDef* b = &t->backgroundMsgs[t->numBackgroundMsgs];

            b->canId = strtoul(msg["canId"] | "0x000", nullptr, 0);
            b->intervalMs = msg["intervalMs"] | 10;

            if (msg.containsKey("data")) {
                String dataStr = msg["data"].as<String>();
                b->len = parseHexBytes(dataStr.c_str(), b->data, 8);
            }

            t->numBackgroundMsgs++;
        }
    }

    return true;
}

void TemplateManager::templateToJson(const Template* t, char* buffer, size_t bufferSize) {
    StaticJsonDocument<4096> doc;

    doc["id"] = t->id;
    doc["name"] = t->name;
    doc["version"] = t->version;

    // Buttons
    JsonObject buttons = doc.createNestedObject("buttons");
    char hexBuf[32];
    snprintf(hexBuf, sizeof(hexBuf), "0x%02X", (unsigned int)t->buttonCanId);
    buttons["canId"] = hexBuf;
    buttons["holdMs"] = t->buttonHoldMs;
    buttons["intervalMs"] = t->buttonIntervalMs;

    bytesToHexString(t->buttonDefault, 8, hexBuf, sizeof(hexBuf));
    buttons["default"] = hexBuf;

    JsonObject cmds = buttons.createNestedObject("commands");
    for (int i = 0; i < t->numButtons; i++) {
        bytesToHexString(t->buttons[i].data, 8, hexBuf, sizeof(hexBuf));
        cmds[t->buttons[i].name] = hexBuf;
    }

    // Gauges
    JsonObject gauges = doc.createNestedObject("gauges");
    for (int i = 0; i < t->numGauges; i++) {
        JsonObject g = gauges.createNestedObject(t->gauges[i].name);
        snprintf(hexBuf, sizeof(hexBuf), "0x%03X", (unsigned int)t->gauges[i].canId);
        g["canId"] = hexBuf;
        bytesToHexString(t->gauges[i].base, 8, hexBuf, sizeof(hexBuf));
        g["base"] = hexBuf;

        JsonArray vb = g.createNestedArray("valueBytes");
        for (int j = 0; j < t->gauges[i].numValueBytes; j++) {
            vb.add(t->gauges[i].valueBytes[j]);
        }

        g["scale"] = t->gauges[i].scale;
        g["offset"] = t->gauges[i].offset;
        g["min"] = t->gauges[i].minVal;
        g["max"] = t->gauges[i].maxVal;
        g["intervalMs"] = t->gauges[i].intervalMs;
    }

    // Temperature
    JsonObject temp = doc.createNestedObject("temperature");
    snprintf(hexBuf, sizeof(hexBuf), "0x%03X", (unsigned int)t->tempCanId);
    temp["canId"] = hexBuf;
    bytesToHexString(t->tempBase, 8, hexBuf, sizeof(hexBuf));
    temp["base"] = hexBuf;
    temp["intervalMs"] = t->tempIntervalMs;

    JsonObject sensors = temp.createNestedObject("sensors");
    for (int i = 0; i < t->numTempSensors; i++) {
        JsonObject s = sensors.createNestedObject(t->tempSensors[i].name);
        s["byte"] = t->tempSensors[i].byteIndex;
    }

    // TPMS
    JsonObject tpms = doc.createNestedObject("tpms");
    snprintf(hexBuf, sizeof(hexBuf), "0x%03X", (unsigned int)t->tpmsCanId);
    tpms["canId"] = hexBuf;
    bytesToHexString(t->tpmsBase, 8, hexBuf, sizeof(hexBuf));
    tpms["base"] = hexBuf;
    tpms["intervalMs"] = t->tpmsIntervalMs;

    JsonObject tires = tpms.createNestedObject("tires");
    for (int i = 0; i < t->numTires; i++) {
        JsonObject tire = tires.createNestedObject(t->tires[i].name);
        tire["byte"] = t->tires[i].byteIndex;
        tire["scale"] = t->tires[i].scale;
    }

    // Blinkers
    JsonObject blinkers = doc.createNestedObject("blinkers");
    snprintf(hexBuf, sizeof(hexBuf), "0x%03X", (unsigned int)t->blinkerCanId);
    blinkers["canId"] = hexBuf;
    snprintf(hexBuf, sizeof(hexBuf), "0x%03X", (unsigned int)t->blinkerCanIdAlt);
    blinkers["canIdAlt"] = hexBuf;
    bytesToHexString(t->blinkerBase, 8, hexBuf, sizeof(hexBuf));
    blinkers["base"] = hexBuf;
    blinkers["intervalMs"] = t->blinkerIntervalMs;
    blinkers["blinkRateMs"] = t->blinkerBlinkRateMs;

    JsonObject left = blinkers.createNestedObject("left");
    left["byte"] = t->blinker.leftByte;
    snprintf(hexBuf, sizeof(hexBuf), "0x%02X", t->blinker.leftMask);
    left["mask"] = hexBuf;

    JsonObject right = blinkers.createNestedObject("right");
    right["byte"] = t->blinker.rightByte;
    snprintf(hexBuf, sizeof(hexBuf), "0x%02X", t->blinker.rightMask);
    right["mask"] = hexBuf;

    // VIN
    JsonObject vin = doc.createNestedObject("vin");
    snprintf(hexBuf, sizeof(hexBuf), "0x%03X", (unsigned int)t->vinCanId);
    vin["canId"] = hexBuf;
    vin["intervalMs"] = t->vinIntervalMs;
    snprintf(hexBuf, sizeof(hexBuf), "%02X", t->vinFramePrefix);
    vin["framePrefix"] = hexBuf;

    // Background messages
    JsonArray bg = doc.createNestedArray("background");
    for (int i = 0; i < t->numBackgroundMsgs; i++) {
        JsonObject msg = bg.createNestedObject();
        snprintf(hexBuf, sizeof(hexBuf), "0x%03X", (unsigned int)t->backgroundMsgs[i].canId);
        msg["canId"] = hexBuf;
        bytesToHexString(t->backgroundMsgs[i].data, t->backgroundMsgs[i].len, hexBuf, sizeof(hexBuf));
        msg["data"] = hexBuf;
        msg["intervalMs"] = t->backgroundMsgs[i].intervalMs;
    }

    serializeJsonPretty(doc, buffer, bufferSize);
}

// Helper functions for hex parsing
static int parseHexBytes(const char* str, byte* out, int maxBytes) {
    int count = 0;
    const char* p = str;

    while (*p && count < maxBytes) {
        // Skip spaces
        while (*p == ' ') p++;
        if (!*p) break;

        // Parse two hex digits
        char hex[3] = {0, 0, 0};
        if (isxdigit(p[0])) {
            hex[0] = p[0];
            p++;
            if (isxdigit(p[0])) {
                hex[1] = p[0];
                p++;
            }
            out[count++] = (byte)strtoul(hex, nullptr, 16);
        } else {
            p++;
        }
    }

    return count;
}

static void bytesToHexString(const byte* data, int len, char* out, size_t outSize) {
    out[0] = '\0';
    for (int i = 0; i < len && (size_t)(i * 3 + 2) < outSize; i++) {
        if (i > 0) strcat(out, " ");
        char hex[4];
        snprintf(hex, sizeof(hex), "%02X", data[i]);
        strcat(out, hex);
    }
}
