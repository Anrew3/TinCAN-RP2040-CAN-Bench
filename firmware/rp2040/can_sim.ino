/*
 * TinCAN CAN-BUS Simulator
 * Template-Based Vehicle Simulator for RP2040
 *
 * Version 2.0.0 - Template System
 *
 * This firmware uses JSON-based templates stored in LittleFS
 * to define vehicle-specific CAN message mappings. Templates
 * can be uploaded via serial and persist across reboots.
 */

#include <SPI.h>
#include "mcp_can.h"
#include "hardware/watchdog.h"

#include "Config.h"
#include "Template.h"
#include "TemplateManager.h"
#include "TemplateRunner.h"

MCP_CAN CAN(CS_PIN);

// Global logging controls
bool g_verboseSerial = false;
unsigned long g_logMinIntervalMs = 0;
static unsigned long g_lastLogTime = 0;

bool canLog(unsigned long now) {
    if (!g_verboseSerial) return false;
    if (g_logMinIntervalMs == 0) return true;
    if (now - g_lastLogTime >= g_logMinIntervalMs) {
        g_lastLogTime = now;
        return true;
    }
    return false;
}

// -------- Custom CAN scheduler --------

struct CustomCanEntry {
    bool used;
    unsigned long id;
    byte data[8];
    byte len;
    unsigned long intervalMs;
    unsigned long lastSent;
    long remaining;
};

CustomCanEntry customCan[MAX_CUSTOM_CAN];

// Template upload state
bool templateUploadMode = false;
size_t templateUploadSize = 0;
size_t templateUploadReceived = 0;
char* templateUploadBuffer = nullptr;

// Forward declarations
void split(const String& s, char delim, String* out, int& count, int maxParts);
void handleSerialLine(const String& line);
unsigned long parseHex(const String& s);
void splitSpaces(const String& s, String* out, int& count, int maxParts);
void scheduleCustomCan(const String& frameDesc, long count, unsigned long intervalMs);
void updateCustomCan(unsigned long now);
void showCustomCan();
void cancelCustomCan(int slotIndex);
void softwareReset();
void factoryReset();
void handleTemplateUpload(const String& data);

void setup() {
    Serial.begin(SERIAL_BAUD);

    // Initialize CAN bus
    if (CAN.begin(MCP_ANY, CAN_SPEED, CAN_CLOCK) == CAN_OK) {
        if (Serial) Serial.println("CAN Initialized Successfully!");
    } else {
        if (Serial) Serial.println("CAN Initialization Failed!");
        while (1); // hard fail if CAN chip isn't happy
    }

    CAN.setMode(MCP_NORMAL);

    // Initialize template manager (loads from LittleFS)
    if (!templateManager.begin()) {
        if (Serial) Serial.println("WARNING: Template storage init failed!");
    }

    // Initialize template runner with CAN bus
    templateRunner.begin(&CAN);
    templateRunner.setTemplate(templateManager.getCurrentTemplate());

    // Init custom CAN table
    for (int i = 0; i < MAX_CUSTOM_CAN; i++) {
        customCan[i].used = false;
    }

    if (Serial) {
        Serial.println();
        Serial.println("========================================");
        Serial.println("   TinCAN CAN-BUS Simulator v2.0.0");
        Serial.println("   Template-Based Vehicle Simulator");
        Serial.println("========================================");
        Serial.println();
        Serial.print("Current Template: ");
        Serial.println(templateManager.getCurrentTemplate()->name);
        Serial.println();
        Serial.println("Commands:");
        Serial.println("  VERSION              - Show firmware version");
        Serial.println("  RESTART              - Restart device");
        Serial.println("  FACTORYRESET         - Reset to factory defaults");
        Serial.println();
        Serial.println("Template Commands:");
        Serial.println("  TEMPLATE:LIST        - List all templates");
        Serial.println("  TEMPLATE:LOAD:<id>   - Switch template");
        Serial.println("  TEMPLATE:SHOW        - Show current template");
        Serial.println("  TEMPLATE:EXPORT      - Export template as JSON");
        Serial.println("  TEMPLATE:UPLOAD:<sz> - Upload template (sz=bytes)");
        Serial.println("  TEMPLATE:DELETE:<id> - Delete custom template");
        Serial.println();
        Serial.println("Vehicle Commands:");
        Serial.println("  RPM:<value>          - Set RPM");
        Serial.println("  SPEED:<value>        - Set Speed");
        Serial.println("  TEMP:COOLANT:<F>     - Set coolant temp");
        Serial.println("  TEMP:OIL:<F>         - Set oil temp");
        Serial.println("  TPMS:FL:<psi>        - Set tire pressure");
        Serial.println("  BLINKER:LEFT/RIGHT/OFF/BOTH");
        Serial.println("  VIN:<17chars>        - Set VIN");
        Serial.println("  UP/DOWN/LEFT/RIGHT/OK/SETTINGS");
        Serial.println();
        Serial.println("Custom CAN:");
        Serial.println("  CAN:<id> <data>:<count>:<interval>");
        Serial.println("  SHOWCAN / CANCELCAN:<slot> / CANSTOP");
        Serial.println();
        Serial.println("Logging:");
        Serial.println("  VERBOSE:ON/OFF       - Toggle verbose mode");
        Serial.println("  LOGRATE:<ms>         - Set log rate limit");
        Serial.println("========================================");
        Serial.println();
    }
}

void loop() {
    unsigned long now = millis();

    // Serial command handling
    if (Serial && Serial.available() > 0) {
        String line = Serial.readStringUntil('\n');
        line.trim();

        if (templateUploadMode) {
            handleTemplateUpload(line);
        } else if (line.length() > 0) {
            handleSerialLine(line);
        }
    }

    // Run the template (sends all periodic CAN messages)
    templateRunner.tick(now);

    // Update custom CAN jobs
    updateCustomCan(now);
}

void handleSerialLine(const String& line) {
    String parts[8];
    int count = 0;
    split(line, ':', parts, count, 8);
    if (count == 0) return;

    String cmd = parts[0];
    cmd.toUpperCase();

    // ---- System Commands ----

    if (cmd == "VERSION") {
        if (Serial) {
            Serial.print("VERSION:");
            Serial.println(TINCAN_VERSION);
        }
        return;
    }

    if (cmd == "RESTART") {
        if (Serial) {
            Serial.println("Restarting TinCAN...");
            Serial.flush();
        }
        delay(100);
        softwareReset();
        return;
    }

    if (cmd == "FACTORYRESET") {
        if (Serial) {
            Serial.println("Factory reset initiated...");
            Serial.flush();
        }
        factoryReset();
        return;
    }

    // ---- Logging Commands ----

    if (cmd == "VERBOSE") {
        if (count >= 2) {
            String v = parts[1];
            v.toUpperCase();
            g_verboseSerial = (v == "ON" || v == "TRUE" || v == "1");
            if (Serial) {
                Serial.print("Verbose logging ");
                Serial.println(g_verboseSerial ? "ENABLED" : "DISABLED");
            }
        } else {
            if (Serial) Serial.println("Usage: VERBOSE:ON|OFF");
        }
        return;
    }

    if (cmd == "LOGRATE") {
        if (count >= 2) {
            g_logMinIntervalMs = (unsigned long)parts[1].toInt();
            if (Serial) {
                Serial.print("Log rate limit: ");
                Serial.print(g_logMinIntervalMs);
                Serial.println(" ms");
            }
        }
        return;
    }

    // ---- Template Commands ----

    if (cmd == "TEMPLATE") {
        if (count < 2) {
            if (Serial) Serial.println("Usage: TEMPLATE:<command>");
            return;
        }

        String subcmd = parts[1];
        subcmd.toUpperCase();

        if (subcmd == "LIST") {
            templateManager.listTemplates();
            return;
        }

        if (subcmd == "LOAD" && count >= 3) {
            if (templateManager.loadTemplate(parts[2].c_str())) {
                templateRunner.setTemplate(templateManager.getCurrentTemplate());
                if (Serial) {
                    Serial.print("OK: Loaded ");
                    Serial.println(templateManager.getCurrentTemplate()->name);
                }
            } else {
                if (Serial) Serial.println("ERROR: Template not found");
            }
            return;
        }

        if (subcmd == "SHOW") {
            Template* t = templateManager.getCurrentTemplate();
            if (Serial) {
                Serial.println("Current Template:");
                Serial.print("  ID: ");
                Serial.println(t->id);
                Serial.print("  Name: ");
                Serial.println(t->name);
                Serial.print("  Version: ");
                Serial.println(t->version);
                Serial.print("  Official: ");
                Serial.println(t->isOfficial ? "Yes" : "No");
                Serial.print("  Gauges: ");
                Serial.println(t->numGauges);
                Serial.print("  Buttons: ");
                Serial.println(t->numButtons);
            }
            return;
        }

        if (subcmd == "EXPORT") {
            templateManager.exportCurrentTemplate();
            return;
        }

        if (subcmd == "UPLOAD" && count >= 3) {
            templateUploadSize = parts[2].toInt();
            if (templateUploadSize <= 0 || templateUploadSize > MAX_TEMPLATE_SIZE) {
                if (Serial) {
                    Serial.print("ERROR: Invalid size (max ");
                    Serial.print(MAX_TEMPLATE_SIZE);
                    Serial.println(" bytes)");
                }
                return;
            }

            templateUploadBuffer = (char*)malloc(templateUploadSize + 1);
            if (!templateUploadBuffer) {
                if (Serial) Serial.println("ERROR: Memory allocation failed");
                return;
            }

            templateUploadReceived = 0;
            templateUploadMode = true;

            if (Serial) {
                Serial.println("TEMPLATE_UPLOAD_READY");
                Serial.print("Send ");
                Serial.print(templateUploadSize);
                Serial.println(" bytes of JSON, end with TEMPLATE_UPLOAD_END");
            }
            return;
        }

        if (subcmd == "DELETE" && count >= 3) {
            if (templateManager.deleteTemplate(parts[2].c_str())) {
                if (Serial) Serial.println("OK: Template deleted");
            } else {
                if (Serial) Serial.println("ERROR: Cannot delete template");
            }
            return;
        }

        if (Serial) Serial.println("Unknown TEMPLATE command");
        return;
    }

    // ---- Custom CAN Commands ----

    if (cmd == "CAN") {
        if (count < 2) {
            if (Serial) Serial.println("Usage: CAN:<id data>[:count][:interval]");
            return;
        }

        String frameDesc = parts[1];
        long repeat = (count >= 3) ? parts[2].toInt() : 1;
        unsigned long intervalMs = (count >= 4) ? (unsigned long)parts[3].toInt() : 10;
        if (intervalMs == 0) intervalMs = 1;

        scheduleCustomCan(frameDesc, repeat, intervalMs);
        return;
    }

    if (cmd == "SHOWCAN") {
        showCustomCan();
        return;
    }

    if (cmd == "CANCELCAN" && count >= 2) {
        cancelCustomCan(parts[1].toInt());
        return;
    }

    if (cmd == "CANSTOP") {
        for (int i = 0; i < MAX_CUSTOM_CAN; i++) {
            customCan[i].used = false;
        }
        if (Serial) Serial.println("Stopped all custom CAN jobs");
        return;
    }

    // ---- Vehicle Commands (dispatched to template runner) ----
    templateRunner.handleCommand(parts, count);
}

void handleTemplateUpload(const String& data) {
    if (data == "TEMPLATE_UPLOAD_END") {
        templateUploadMode = false;
        templateUploadBuffer[templateUploadReceived] = '\0';

        if (Serial) {
            Serial.print("Received ");
            Serial.print(templateUploadReceived);
            Serial.println(" bytes, parsing...");
        }

        if (templateManager.importTemplateFromJson(templateUploadBuffer, templateUploadReceived)) {
            templateRunner.setTemplate(templateManager.getCurrentTemplate());
            if (Serial) {
                Serial.println("TEMPLATE_UPLOAD_OK");
                Serial.print("Loaded: ");
                Serial.println(templateManager.getCurrentTemplate()->name);
            }
        } else {
            if (Serial) Serial.println("TEMPLATE_UPLOAD_ERROR: Parse failed");
        }

        free(templateUploadBuffer);
        templateUploadBuffer = nullptr;
        templateUploadReceived = 0;
        return;
    }

    if (data == "TEMPLATE_UPLOAD_CANCEL") {
        templateUploadMode = false;
        free(templateUploadBuffer);
        templateUploadBuffer = nullptr;
        templateUploadReceived = 0;
        if (Serial) Serial.println("Template upload cancelled");
        return;
    }

    // Append data to buffer
    size_t len = data.length();
    if (templateUploadReceived + len + 1 > templateUploadSize) {
        len = templateUploadSize - templateUploadReceived;
    }

    if (len > 0) {
        memcpy(templateUploadBuffer + templateUploadReceived, data.c_str(), len);
        templateUploadReceived += len;
        templateUploadBuffer[templateUploadReceived] = '\0';

        // Add newline between lines
        if (templateUploadReceived < templateUploadSize) {
            templateUploadBuffer[templateUploadReceived++] = '\n';
        }
    }
}

// -------- Utility Functions --------

void split(const String& s, char delim, String* out, int& count, int maxParts) {
    count = 0;
    int start = 0;
    while (count < maxParts) {
        int idx = s.indexOf(delim, start);
        if (idx == -1) {
            out[count++] = s.substring(start);
            break;
        }
        out[count++] = s.substring(start, idx);
        start = idx + 1;
    }
}

unsigned long parseHex(const String& sIn) {
    String s = sIn;
    s.trim();
    if (s.startsWith("0x") || s.startsWith("0X")) {
        s = s.substring(2);
    }
    unsigned long value = 0;
    for (unsigned int i = 0; i < s.length(); i++) {
        char c = s[i];
        int digit = -1;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F') digit = 10 + (c - 'A');
        else break;
        value = (value << 4) | (unsigned long)digit;
    }
    return value;
}

void splitSpaces(const String& s, String* out, int& count, int maxParts) {
    count = 0;
    int len = s.length();
    int i = 0;

    while (i < len && count < maxParts) {
        while (i < len && s[i] == ' ') i++;
        if (i >= len) break;
        int start = i;
        while (i < len && s[i] != ' ') i++;
        out[count++] = s.substring(start, i);
    }
}

void scheduleCustomCan(const String& frameDesc, long count, unsigned long intervalMs) {
    String tokens[9];
    int n = 0;
    splitSpaces(frameDesc, tokens, n, 9);
    if (n < 1) {
        if (Serial) Serial.println("CAN error: missing ID");
        return;
    }

    unsigned long id = parseHex(tokens[0]);
    byte data[8] = {0};
    byte len = (n - 1) > 8 ? 8 : (n - 1);

    for (int i = 0; i < len; i++) {
        data[i] = (byte)(parseHex(tokens[i + 1]) & 0xFF);
    }

    if (intervalMs == 0) intervalMs = 1;

    int slot = -1;
    for (int i = 0; i < MAX_CUSTOM_CAN; i++) {
        if (!customCan[i].used) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        if (Serial) Serial.println("CAN error: no free slots");
        return;
    }

    customCan[slot].used = true;
    customCan[slot].id = id;
    customCan[slot].len = len;
    memcpy(customCan[slot].data, data, 8);
    customCan[slot].intervalMs = intervalMs;
    customCan[slot].lastSent = millis() - intervalMs;
    customCan[slot].remaining = (count > 0) ? count : -1;

    if (Serial) {
        Serial.print("Scheduled CAN slot ");
        Serial.print(slot);
        Serial.print(": ID=0x");
        Serial.print(id, HEX);
        Serial.print(" interval=");
        Serial.print(intervalMs);
        Serial.print("ms count=");
        if (customCan[slot].remaining < 0) Serial.println("INF");
        else Serial.println(customCan[slot].remaining);
    }
}

void updateCustomCan(unsigned long now) {
    for (int i = 0; i < MAX_CUSTOM_CAN; i++) {
        if (!customCan[i].used) continue;
        if (customCan[i].remaining == 0) {
            customCan[i].used = false;
            continue;
        }

        if (now - customCan[i].lastSent >= customCan[i].intervalMs) {
            CAN.sendMsgBuf(customCan[i].id, 0, customCan[i].len, customCan[i].data);
            customCan[i].lastSent = now;

            if (customCan[i].remaining > 0) {
                customCan[i].remaining--;
                if (customCan[i].remaining == 0) {
                    customCan[i].used = false;
                }
            }
        }
    }
}

void showCustomCan() {
    if (Serial) {
        Serial.println("Active custom CAN slots:");
        bool any = false;
        for (int i = 0; i < MAX_CUSTOM_CAN; i++) {
            if (!customCan[i].used) continue;
            any = true;
            Serial.print("  [");
            Serial.print(i);
            Serial.print("] ID=0x");
            Serial.print(customCan[i].id, HEX);
            Serial.print(" interval=");
            Serial.print(customCan[i].intervalMs);
            Serial.print("ms remaining=");
            if (customCan[i].remaining < 0) Serial.println("INF");
            else Serial.println(customCan[i].remaining);
        }
        if (!any) Serial.println("  (none)");
    }
}

void cancelCustomCan(int slot) {
    if (slot < 0 || slot >= MAX_CUSTOM_CAN) {
        if (Serial) Serial.println("Invalid slot");
        return;
    }
    customCan[slot].used = false;
    if (Serial) {
        Serial.print("Cancelled slot ");
        Serial.println(slot);
    }
}

void softwareReset() {
    watchdog_reboot(0, 0, 0);
    while (1) {}
}

void factoryReset() {
    for (int i = 0; i < MAX_CUSTOM_CAN; i++) {
        customCan[i].used = false;
    }

    g_verboseSerial = false;
    g_logMinIntervalMs = 0;

    templateManager.factoryReset();
    templateRunner.setTemplate(templateManager.getCurrentTemplate());

    if (Serial) {
        Serial.println("Factory reset complete. Restarting...");
        Serial.flush();
    }

    delay(100);
    softwareReset();
}
