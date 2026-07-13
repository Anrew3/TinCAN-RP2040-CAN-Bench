/*
 * TinCAN CAN-BUS Simulator
 * Template-Based Vehicle Simulator for RP2040
 *
 * Version 2.1.0 - Template System + Sniffer/SavvyCAN
 *
 * This firmware uses JSON-based templates stored in LittleFS
 * to define vehicle-specific CAN message mappings. Templates
 * can be uploaded via serial and persist across reboots.
 *
 * 2.1.0 adds: BOOTLOADER + STATUS commands, CAN receive
 * (candump-format sniffer + GVRET binary protocol for
 * SavvyCAN), gauge sweeps + demo drive cycle, persisted
 * settings, and CRC-verified template uploads.
 */

#include <SPI.h>
#include <LittleFS.h>
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

// Global state flags
bool g_canOk = false;
bool g_txSuspended = false;
bool g_quietSet = false;

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
unsigned long templateUploadExpectedCrc = 0;

// Serial line assembly (non-blocking; sized for a full template upload line)
static char lineBuf[MAX_TEMPLATE_SIZE + 128];
static size_t lineLen = 0;

// CAN sniffer state
bool sniffEnabled = false;
bool sniffFilterOn = false;
unsigned long sniffFilterId = 0;

// GVRET binary protocol (SavvyCAN) state
bool gvretMode = false;

// Gauge sweep engine
struct SweepJob {
    bool used;
    char gauge[16];
    long from;
    long to;
    unsigned long durMs;
    unsigned long startMs;
    long cycles;            // 0 = ping-pong forever
    unsigned long lastUpdate;
};
SweepJob sweeps[MAX_SWEEPS];

// Demo drive-cycle state
bool demoActive = false;
unsigned long demoStartMs = 0;
int demoIndex = 0;

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
void pollSerial();
void pollCanRx(unsigned long now);
void loadSettings();
void saveSettings();
void printStatus();
void setGaugeValue(const char* name, long value);
void startSweep(const String& gauge, long from, long to, unsigned long durMs, long cycles);
void stopSweeps(const char* gauge);
void sweepTick(unsigned long now);
void demoTick(unsigned long now);
void enterGvretMode();
void exitGvretMode();
void gvretProcessByte(uint8_t inByte);
void gvretSendFrameToHost(unsigned long id, bool ext, bool rtr, byte len, const byte* data);
void gvretApplyBusConfig(unsigned long cfg);
unsigned long crc32buf(const char* data, size_t len);

void setup() {
    Serial.begin(SERIAL_BAUD);

    // Initialize CAN bus. On failure keep running in degraded mode so the
    // serial console, STATUS diagnostics, and BOOTLOADER stay reachable.
    g_canOk = (CAN.begin(MCP_ANY, CAN_SPEED, CAN_CLOCK) == CAN_OK);
    if (g_canOk) {
        CAN.setMode(MCP_NORMAL);
        if (Serial) Serial.println("CAN Initialized Successfully!");
    } else {
        if (Serial) Serial.println("ERROR: CAN Initialization Failed! Check MCP2515 wiring. Serial console still active.");
    }

    // Initialize template manager (loads from LittleFS)
    if (!templateManager.begin()) {
        if (Serial) Serial.println("WARNING: Template storage init failed!");
    }

    // Load persisted settings (verbose, log rate)
    loadSettings();

    // Initialize template runner with CAN bus
    templateRunner.begin(&CAN);
    templateRunner.setTemplate(templateManager.getCurrentTemplate());

    // Init custom CAN table
    for (int i = 0; i < MAX_CUSTOM_CAN; i++) {
        customCan[i].used = false;
    }

    // Init sweep table
    for (int i = 0; i < MAX_SWEEPS; i++) {
        sweeps[i].used = false;
    }

    if (Serial) {
        Serial.println();
        Serial.println("========================================");
        Serial.print("   TinCAN CAN-BUS Simulator v");
        Serial.println(TINCAN_VERSION);
        Serial.println("   Template-Based Vehicle Simulator");
        Serial.println("========================================");
        Serial.println();
        Serial.print("Current Template: ");
        Serial.println(templateManager.getCurrentTemplate()->name);
        Serial.println();
        Serial.println("Commands:");
        Serial.println("  VERSION              - Show firmware version");
        Serial.println("  STATUS               - Show device status (JSON)");
        Serial.println("  RESTART              - Restart device");
        Serial.println("  BOOTLOADER           - Reboot to UF2 flash mode");
        Serial.println("  FACTORYRESET         - Reset to factory defaults");
        Serial.println();
        Serial.println("Template Commands:");
        Serial.println("  TEMPLATE:LIST        - List all templates");
        Serial.println("  TEMPLATE:LOAD:<id>   - Switch template");
        Serial.println("  TEMPLATE:SHOW        - Show current template");
        Serial.println("  TEMPLATE:EXPORT      - Export template as JSON");
        Serial.println("  TEMPLATE:UPLOAD:<sz> - Upload template (sz=bytes)");
        Serial.println("  TEMPLATE:DELETE:<id> - Delete custom template");
        Serial.println("  BOOTSEQ              - Replay template boot sequence");
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
        Serial.println("Sniffer / SavvyCAN:");
        Serial.println("  SNIFF:ON/OFF         - Stream RX frames (candump format)");
        Serial.println("  SNIFF:FILTER:<id>    - Only show one CAN ID (hex)");
        Serial.println("  SNIFF:NOFILTER       - Clear the filter");
        Serial.println("  (SavvyCAN: connect as GVRET serial device)");
        Serial.println();
        Serial.println("Simulation:");
        Serial.println("  SWEEP:<gauge>:<from>:<to>:<ms>[:cycles]");
        Serial.println("  SWEEP:STOP[:<gauge>] - Stop sweeps (0 cycles = forever)");
        Serial.println("  DEMO:ON/OFF          - Scripted drive cycle");
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

    // Serial command handling (non-blocking; also feeds GVRET binary mode)
    pollSerial();

    // Leave GVRET mode when the host drops the connection
    if (gvretMode && (!Serial || !Serial.dtr())) {
        exitGvretMode();
    }

    // Receive CAN frames for the sniffer / SavvyCAN
    pollCanRx(now);

    // Value sweeps and demo drive cycle
    sweepTick(now);
    demoTick(now);

    if (g_canOk && !g_txSuspended) {
        // Run the template (sends all periodic CAN messages)
        templateRunner.tick(now);

        // Update custom CAN jobs
        updateCustomCan(now);
    }
}

void pollSerial() {
    while (Serial && Serial.available() > 0) {
        int c = Serial.read();
        if (c < 0) break;

        if (gvretMode) {
            gvretProcessByte((uint8_t)c);
            continue;
        }

        // SavvyCAN opens with binary GVRET traffic (0xE7 enables binary
        // mode, 0xF1 prefixes every command) - neither appears in text
        // commands, so use them to switch protocols automatically.
        if (lineLen == 0 && !templateUploadMode && (c == 0xE7 || c == 0xF1)) {
            enterGvretMode();
            gvretProcessByte((uint8_t)c);
            continue;
        }

        if (c == '\r') continue;

        if (c == '\n') {
            lineBuf[lineLen] = '\0';
            String line(lineBuf);
            lineLen = 0;

            if (templateUploadMode) {
                handleTemplateUpload(line);
            } else {
                line.trim();
                if (line.length() > 0) handleSerialLine(line);
            }
        } else if (lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = (char)c;
        }
    }
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

    if (cmd == "STATUS") {
        printStatus();
        return;
    }

    if (cmd == "BOOTLOADER") {
        if (Serial) {
            Serial.println("OK: Rebooting to BOOTSEL mode - RPI-RP2 drive will appear...");
            Serial.flush();
        }
        delay(100);
        rp2040.rebootToBootloader();
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

    if (cmd == "BOOTSEQ") {
        templateRunner.startBootSequence();
        if (Serial) Serial.println("OK: Replaying template boot sequence");
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
            saveSettings();
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
            saveSettings();
            if (Serial) {
                Serial.print("Log rate limit: ");
                Serial.print(g_logMinIntervalMs);
                Serial.println(" ms");
            }
        }
        return;
    }

    // ---- Sniffer Commands ----

    if (cmd == "SNIFF") {
        if (count < 2) {
            if (Serial) Serial.println("Usage: SNIFF:ON|OFF|FILTER:<id>|NOFILTER");
            return;
        }
        String sub = parts[1];
        sub.toUpperCase();

        if (sub == "ON") {
            if (!g_canOk) {
                if (Serial) Serial.println("ERROR: CAN not initialized");
                return;
            }
            sniffEnabled = true;
            if (Serial) Serial.println("Sniffer ON (candump format: \"(sec.usec) can0 ID#DATA\")");
        } else if (sub == "OFF") {
            sniffEnabled = false;
            if (Serial) Serial.println("Sniffer OFF");
        } else if (sub == "FILTER" && count >= 3) {
            sniffFilterId = parseHex(parts[2]);
            sniffFilterOn = true;
            if (Serial) {
                Serial.print("Sniffer filter: 0x");
                Serial.println(sniffFilterId, HEX);
            }
        } else if (sub == "NOFILTER") {
            sniffFilterOn = false;
            if (Serial) Serial.println("Sniffer filter cleared");
        } else {
            if (Serial) Serial.println("Usage: SNIFF:ON|OFF|FILTER:<id>|NOFILTER");
        }
        return;
    }

    // ---- Simulation Commands ----

    if (cmd == "SWEEP") {
        if (count >= 2) {
            String sub = parts[1];
            sub.toUpperCase();
            if (sub == "STOP") {
                stopSweeps(count >= 3 ? parts[2].c_str() : nullptr);
                return;
            }
        }

        if (count < 5) {
            if (Serial) Serial.println("Usage: SWEEP:<gauge>:<from>:<to>:<ms>[:cycles] or SWEEP:STOP[:<gauge>]");
            return;
        }

        long from = parts[2].toInt();
        long to = parts[3].toInt();
        long durMs = parts[4].toInt();
        long cycles = (count >= 6) ? parts[5].toInt() : 1;
        startSweep(parts[1], from, to, (unsigned long)durMs, cycles);
        return;
    }

    if (cmd == "DEMO") {
        if (count < 2) {
            if (Serial) Serial.println("Usage: DEMO:ON|OFF");
            return;
        }
        String sub = parts[1];
        sub.toUpperCase();

        if (sub == "ON" || sub == "START") {
            demoActive = true;
            demoStartMs = millis();
            demoIndex = 0;
            if (Serial) Serial.println("Demo drive cycle started (DEMO:OFF to stop)");
        } else {
            demoActive = false;
            stopSweeps(nullptr);
            setGaugeValue("RPM", 800);
            setGaugeValue("SPEED", 0);
            String off[2] = {String("BLINKER"), String("OFF")};
            templateRunner.handleCommand(off, 2);
            if (Serial) Serial.println("Demo drive cycle stopped");
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

            // Optional CRC32 (hex) of the payload for integrity checking
            templateUploadExpectedCrc = (count >= 4) ? parseHex(parts[3]) : 0;

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

        // Verify integrity if the sender provided a CRC32
        if (templateUploadExpectedCrc != 0) {
            unsigned long got = crc32buf(templateUploadBuffer, templateUploadReceived);
            if (got != templateUploadExpectedCrc) {
                if (Serial) {
                    Serial.print("TEMPLATE_UPLOAD_ERROR: CRC mismatch (got ");
                    Serial.print(got, HEX);
                    Serial.print(", expected ");
                    Serial.print(templateUploadExpectedCrc, HEX);
                    Serial.println(")");
                }
                free(templateUploadBuffer);
                templateUploadBuffer = nullptr;
                templateUploadReceived = 0;
                return;
            }
            if (Serial) Serial.println("CRC OK");
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
    LittleFS.remove("/settings.cfg");

    templateManager.factoryReset();
    templateRunner.setTemplate(templateManager.getCurrentTemplate());

    if (Serial) {
        Serial.println("Factory reset complete. Restarting...");
        Serial.flush();
    }

    delay(100);
    softwareReset();
}

// -------- Settings Persistence --------

#define SETTINGS_FILE "/settings.cfg"

void loadSettings() {
    File f = LittleFS.open(SETTINGS_FILE, "r");
    if (!f) return;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        int eq = line.indexOf('=');
        if (eq <= 0) continue;

        String key = line.substring(0, eq);
        String val = line.substring(eq + 1);

        if (key == "verbose") g_verboseSerial = (val.toInt() != 0);
        else if (key == "lograte") g_logMinIntervalMs = (unsigned long)val.toInt();
    }
    f.close();
}

void saveSettings() {
    File f = LittleFS.open(SETTINGS_FILE, "w");
    if (!f) {
        if (Serial) Serial.println("WARNING: Could not save settings");
        return;
    }
    f.print("verbose=");
    f.println(g_verboseSerial ? 1 : 0);
    f.print("lograte=");
    f.println(g_logMinIntervalMs);
    f.close();
}

// -------- Status Report --------

void printStatus() {
    if (!Serial) return;

    FSInfo fsInfo;
    bool fsOk = LittleFS.info(fsInfo);
    Template* t = templateManager.getCurrentTemplate();

    Serial.print("STATUS:{");
    Serial.print("\"version\":\"");
    Serial.print(TINCAN_VERSION);
    Serial.print("\",\"template\":\"");
    Serial.print(templateManager.getCurrentTemplateId());
    Serial.print("\",\"templateName\":\"");
    Serial.print(t->name);
    Serial.print("\",\"canOk\":");
    Serial.print(g_canOk ? "true" : "false");
    Serial.print(",\"txSuspended\":");
    Serial.print(g_txSuspended ? "true" : "false");
    Serial.print(",\"sniff\":");
    Serial.print(sniffEnabled ? "true" : "false");
    Serial.print(",\"demo\":");
    Serial.print(demoActive ? "true" : "false");
    Serial.print(",\"freeHeap\":");
    Serial.print((unsigned int)rp2040.getFreeHeap());
    if (fsOk) {
        Serial.print(",\"fsUsedBytes\":");
        Serial.print((unsigned int)fsInfo.usedBytes);
        Serial.print(",\"fsTotalBytes\":");
        Serial.print((unsigned int)fsInfo.totalBytes);
    }
    Serial.print(",\"verbose\":");
    Serial.print(g_verboseSerial ? "true" : "false");
    Serial.print(",\"logRateMs\":");
    Serial.print(g_logMinIntervalMs);
    Serial.print(",\"uptimeMs\":");
    Serial.print(millis());
    Serial.println("}");
}

// -------- CAN Receive (Sniffer + GVRET) --------

void pollCanRx(unsigned long now) {
    (void)now;
    if (!g_canOk) return;
    if (!sniffEnabled && !gvretMode) return;

    // Bounded per loop pass so a busy bus can't starve the TX schedulers
    int guard = 16;
    while (guard-- > 0 && CAN.checkReceive() == CAN_MSGAVAIL) {
        unsigned long rawId = 0;
        byte len = 0;
        byte buf[8];
        if (CAN.readMsgBuf(&rawId, &len, buf) != CAN_OK) break;
        if (len > 8) len = 8;

        bool ext = (rawId & 0x80000000UL) != 0;
        bool rtr = (rawId & 0x40000000UL) != 0;
        unsigned long id = rawId & 0x1FFFFFFFUL;

        if (gvretMode) {
            gvretSendFrameToHost(id, ext, rtr, len, buf);
            continue;
        }

        if (sniffFilterOn && id != sniffFilterId) continue;

        // candump-compatible: "(sec.usec) can0 ID#DATA" - imports into SavvyCAN
        if (Serial) {
            unsigned long us = micros();
            char head[40];
            if (ext) {
                snprintf(head, sizeof(head), "(%lu.%06lu) can0 %08lX#",
                         us / 1000000UL, us % 1000000UL, id);
            } else {
                snprintf(head, sizeof(head), "(%lu.%06lu) can0 %03lX#",
                         us / 1000000UL, us % 1000000UL, id);
            }
            Serial.print(head);
            if (rtr) {
                Serial.print('R');
            } else {
                for (int i = 0; i < len; i++) {
                    char hex[3];
                    snprintf(hex, sizeof(hex), "%02X", buf[i]);
                    Serial.print(hex);
                }
            }
            Serial.println();
        }
    }
}

// -------- GVRET Binary Protocol (SavvyCAN) --------
//
// Implements the GVRET serial protocol so SavvyCAN can connect to TinCAN
// directly (Connection -> Add New Device Connection -> Serial Connection /
// GVRET). Frame formats match collin80/ESP32RET's gvret_comm.cpp.

enum GvretState : uint8_t {
    GVRET_IDLE,
    GVRET_GET_COMMAND,
    GVRET_BUILD_FRAME,
    GVRET_ECHO_FRAME,
    GVRET_SETUP_CANBUS,
    GVRET_CONSUME
};

static GvretState gvretState = GVRET_IDLE;
static uint8_t gvretStep = 0;
static uint8_t gvretConsumeLeft = 0;
static uint8_t gvretCfgBuf[8];
static unsigned long gvretFrameId = 0;
static uint8_t gvretFrameLen = 0;
static uint8_t gvretFrameData[8];
static unsigned long gvretBusSpeed = 500000;
static bool gvretListenOnly = false;
static bool gvretPrevVerbose = false;

void enterGvretMode() {
    gvretMode = true;
    gvretState = GVRET_IDLE;
    gvretStep = 0;
    // Text logging would corrupt the binary stream
    gvretPrevVerbose = g_verboseSerial;
    g_verboseSerial = false;
    sniffEnabled = false;
}

void exitGvretMode() {
    gvretMode = false;
    gvretState = GVRET_IDLE;
    g_verboseSerial = gvretPrevVerbose;
    if (gvretListenOnly && g_canOk) {
        CAN.setMode(MCP_NORMAL);
    }
    gvretListenOnly = false;
    g_txSuspended = false;
}

void gvretSendFrameToHost(unsigned long id, bool ext, bool rtr, byte len, const byte* data) {
    if (rtr) return; // GVRET stream carries data frames only
    uint8_t out[20];
    int i = 0;
    uint32_t now = micros();
    uint32_t outId = id | (ext ? 0x80000000UL : 0);

    out[i++] = 0xF1;
    out[i++] = 0x00; // 0 = CAN frame to host
    out[i++] = (uint8_t)(now & 0xFF);
    out[i++] = (uint8_t)(now >> 8);
    out[i++] = (uint8_t)(now >> 16);
    out[i++] = (uint8_t)(now >> 24);
    out[i++] = (uint8_t)(outId & 0xFF);
    out[i++] = (uint8_t)(outId >> 8);
    out[i++] = (uint8_t)(outId >> 16);
    out[i++] = (uint8_t)(outId >> 24);
    out[i++] = (uint8_t)(len & 0xF); // length | (bus << 4), bus 0
    for (int c = 0; c < len; c++) out[i++] = data[c];
    out[i++] = 0; // checksum placeholder (matches reference firmware)

    Serial.write(out, i);
}

void gvretApplyBusConfig(unsigned long cfg) {
    bool enabled = true;
    bool listenOnly = false;

    if (cfg & 0x80000000UL) {
        enabled = (cfg & 0x40000000UL) != 0;
        listenOnly = (cfg & 0x20000000UL) != 0;
    }
    unsigned long speed = cfg & 0xFFFFFUL;

    byte mcpSpeed = 0xFF;
    switch (speed) {
        case 33333:   mcpSpeed = CAN_33K3BPS;  break;
        case 100000:  mcpSpeed = CAN_100KBPS;  break;
        case 125000:  mcpSpeed = CAN_125KBPS;  break;
        case 200000:  mcpSpeed = CAN_200KBPS;  break;
        case 250000:  mcpSpeed = CAN_250KBPS;  break;
        case 500000:  mcpSpeed = CAN_500KBPS;  break;
        case 1000000: mcpSpeed = CAN_1000KBPS; break;
        default: break;
    }

    if (mcpSpeed != 0xFF && speed != gvretBusSpeed) {
        if (CAN.begin(MCP_ANY, mcpSpeed, CAN_CLOCK) == CAN_OK) {
            gvretBusSpeed = speed;
            g_canOk = true;
        }
    }

    if (g_canOk) {
        CAN.setMode(listenOnly ? MCP_LISTENONLY : MCP_NORMAL);
    }
    gvretListenOnly = listenOnly;
    // In listen-only (or bus disabled) the simulator must stay silent
    g_txSuspended = listenOnly || !enabled;
}

void gvretProcessByte(uint8_t inByte) {
    switch (gvretState) {
    case GVRET_IDLE:
        if (inByte == 0xF1) gvretState = GVRET_GET_COMMAND;
        // 0xE7 (re)confirms binary mode; nothing to do
        break;

    case GVRET_GET_COMMAND:
        gvretState = GVRET_IDLE;
        gvretStep = 0;
        switch (inByte) {
        case 0: // BUILD_CAN_FRAME (host wants us to transmit)
        case 11: { // ECHO_CAN_FRAME (parse identically, echo back)
            gvretState = (inByte == 0) ? GVRET_BUILD_FRAME : GVRET_ECHO_FRAME;
            gvretFrameId = 0;
            gvretFrameLen = 0;
            break;
        }
        case 1: { // TIME_SYNC
            uint32_t now = micros();
            uint8_t r[6] = {0xF1, 1, (uint8_t)(now & 0xFF), (uint8_t)(now >> 8),
                            (uint8_t)(now >> 16), (uint8_t)(now >> 24)};
            Serial.write(r, 6);
            break;
        }
        case 2: { // GET_DIG_INPUTS: value + checksum
            uint8_t r[4] = {0xF1, 2, 0, 0};
            Serial.write(r, 4);
            break;
        }
        case 3: { // GET_ANALOG_INPUTS: 6 x 16-bit + checksum
            uint8_t r[15] = {0xF1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            Serial.write(r, 15);
            break;
        }
        case 4: // SET_DIG_OUTPUTS: consume 1 byte
            gvretState = GVRET_CONSUME;
            gvretConsumeLeft = 1;
            break;
        case 5: // SETUP_CANBUS: two 4-byte bus configs
            gvretState = GVRET_SETUP_CANBUS;
            break;
        case 6: { // GET_CANBUS_PARAMS
            uint8_t r[12];
            r[0] = 0xF1;
            r[1] = 6;
            r[2] = 1 | (gvretListenOnly ? 0x10 : 0);
            r[3] = (uint8_t)(gvretBusSpeed & 0xFF);
            r[4] = (uint8_t)(gvretBusSpeed >> 8);
            r[5] = (uint8_t)(gvretBusSpeed >> 16);
            r[6] = (uint8_t)(gvretBusSpeed >> 24);
            r[7] = 0;  // bus 1 disabled
            r[8] = 0;
            r[9] = 0;
            r[10] = 0;
            r[11] = 0;
            Serial.write(r, 12);
            break;
        }
        case 7: { // GET_DEVICE_INFO: build number + flags
            uint8_t r[8] = {0xF1, 7, 0x66, 0x02, 0x20, 0, 0, 0};
            Serial.write(r, 8);
            break;
        }
        case 8: // SET_SW_MODE: consume 1 byte
        case 10: // SET_SYSTYPE: consume 1 byte
            gvretState = GVRET_CONSUME;
            gvretConsumeLeft = 1;
            break;
        case 9: { // KEEPALIVE
            uint8_t r[4] = {0xF1, 9, 0xDE, 0xAD};
            Serial.write(r, 4);
            break;
        }
        case 12: { // GET_NUMBUSES
            uint8_t r[3] = {0xF1, 12, 1};
            Serial.write(r, 3);
            break;
        }
        case 13: { // GET_EXT_BUSES: buses 2-4 all disabled
            uint8_t r[17];
            memset(r, 0, sizeof(r));
            r[0] = 0xF1;
            r[1] = 13;
            Serial.write(r, 17);
            break;
        }
        case 14: // SET_EXT_BUSES: consume 12 bytes
            gvretState = GVRET_CONSUME;
            gvretConsumeLeft = 12;
            break;
        default:
            break; // unknown command - back to idle
        }
        break;

    case GVRET_BUILD_FRAME:
    case GVRET_ECHO_FRAME:
        // id(4 LE, bit31 = extended), bus(1), len(1), data[len], checksum(1)
        if (gvretStep < 4) {
            gvretFrameId |= ((unsigned long)inByte) << (8 * gvretStep);
        } else if (gvretStep == 4) {
            // bus selection - single bus, ignored
        } else if (gvretStep == 5) {
            gvretFrameLen = inByte & 0xF;
            if (gvretFrameLen > 8) gvretFrameLen = 8;
        } else if ((uint8_t)(gvretStep - 6) < gvretFrameLen) {
            gvretFrameData[gvretStep - 6] = inByte;
        } else {
            // checksum byte - frame complete
            bool ext = (gvretFrameId & 0x80000000UL) != 0;
            unsigned long id = gvretFrameId & 0x1FFFFFFFUL;
            if (gvretState == GVRET_BUILD_FRAME) {
                if (g_canOk && !gvretListenOnly) {
                    CAN.sendMsgBuf(id, ext ? 1 : 0, gvretFrameLen, gvretFrameData);
                }
            } else {
                gvretSendFrameToHost(id, ext, false, gvretFrameLen, gvretFrameData);
            }
            gvretState = GVRET_IDLE;
            gvretStep = 0;
            break;
        }
        gvretStep++;
        break;

    case GVRET_SETUP_CANBUS:
        // 8 bytes: bus0 config (4 LE), bus1 config (4 LE, ignored)
        if (gvretStep < 8) gvretCfgBuf[gvretStep] = inByte;
        gvretStep++;
        if (gvretStep >= 8) {
            unsigned long cfg = (unsigned long)gvretCfgBuf[0] |
                                ((unsigned long)gvretCfgBuf[1] << 8) |
                                ((unsigned long)gvretCfgBuf[2] << 16) |
                                ((unsigned long)gvretCfgBuf[3] << 24);
            gvretApplyBusConfig(cfg);
            gvretState = GVRET_IDLE;
            gvretStep = 0;
        }
        break;

    case GVRET_CONSUME:
        if (--gvretConsumeLeft == 0) {
            gvretState = GVRET_IDLE;
            gvretStep = 0;
        }
        break;
    }
}

// -------- Gauge Sweep Engine --------

void setGaugeValue(const char* name, long value) {
    String tokens[2] = {String(name), String(value)};
    g_quietSet = true;
    templateRunner.handleCommand(tokens, 2);
    g_quietSet = false;
}

void startSweep(const String& gauge, long from, long to, unsigned long durMs, long cycles) {
    if (!findGauge(templateManager.getCurrentTemplate(), gauge.c_str())) {
        if (Serial) {
            Serial.print("ERROR: Unknown gauge '");
            Serial.print(gauge);
            Serial.println("' in current template");
        }
        return;
    }
    if (durMs < 100) durMs = 100;
    if (cycles < 0) cycles = 1;

    // Reuse a running sweep on the same gauge, else take a free slot
    int slot = -1;
    for (int i = 0; i < MAX_SWEEPS; i++) {
        if (sweeps[i].used && gauge.equalsIgnoreCase(sweeps[i].gauge)) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        for (int i = 0; i < MAX_SWEEPS; i++) {
            if (!sweeps[i].used) {
                slot = i;
                break;
            }
        }
    }
    if (slot == -1) {
        if (Serial) Serial.println("ERROR: No free sweep slots (SWEEP:STOP to clear)");
        return;
    }

    SweepJob& s = sweeps[slot];
    s.used = true;
    gauge.toCharArray(s.gauge, sizeof(s.gauge));
    s.from = from;
    s.to = to;
    s.durMs = durMs;
    s.startMs = millis();
    s.cycles = cycles;
    s.lastUpdate = 0;

    if (Serial) {
        Serial.print("Sweep started: ");
        Serial.print(s.gauge);
        Serial.print(" ");
        Serial.print(from);
        Serial.print(" -> ");
        Serial.print(to);
        Serial.print(" over ");
        Serial.print(durMs);
        Serial.print("ms x");
        if (cycles == 0) Serial.println("INF");
        else Serial.println(cycles);
    }
}

void stopSweeps(const char* gauge) {
    int stopped = 0;
    for (int i = 0; i < MAX_SWEEPS; i++) {
        if (!sweeps[i].used) continue;
        if (gauge && strcasecmp(sweeps[i].gauge, gauge) != 0) continue;
        sweeps[i].used = false;
        stopped++;
    }
    if (Serial) {
        Serial.print("Stopped ");
        Serial.print(stopped);
        Serial.println(" sweep(s)");
    }
}

void sweepTick(unsigned long now) {
    for (int i = 0; i < MAX_SWEEPS; i++) {
        SweepJob& s = sweeps[i];
        if (!s.used) continue;
        if (now - s.lastUpdate < 40) continue;
        s.lastUpdate = now;

        unsigned long elapsed = now - s.startMs;
        unsigned long pass = elapsed / s.durMs; // completed one-way passes

        if (s.cycles > 0 && pass >= (unsigned long)s.cycles) {
            // Land exactly on the final endpoint
            setGaugeValue(s.gauge, (s.cycles % 2) ? s.to : s.from);
            s.used = false;
            continue;
        }

        float frac = (float)(elapsed % s.durMs) / (float)s.durMs;
        if (pass % 2) frac = 1.0f - frac; // ping-pong on the way back
        long value = s.from + (long)((float)(s.to - s.from) * frac);
        setGaugeValue(s.gauge, value);
    }
}

// -------- Demo Drive Cycle --------

struct DemoStep {
    unsigned long atMs;
    const char* cmd;
};

static const DemoStep DEMO_SCRIPT[] = {
    {0,     "SWEEP:STOP"},
    {100,   "RPM:800"},
    {100,   "SPEED:0"},
    {1500,  "SWEEP:RPM:800:5500:1500:2"},      // rev the engine
    {6000,  "BLINKER:LEFT"},
    {8000,  "SWEEP:RPM:800:3200:5000:1"},      // pull away
    {8000,  "SWEEP:SPEED:0:45:5000:1"},
    {13500, "BLINKER:OFF"},
    {14500, "SWEEP:RPM:3200:2400:4000:1"},     // shift up, cruise
    {14500, "SWEEP:SPEED:45:75:4000:1"},
    {23000, "SWEEP:RPM:2400:800:6000:1"},      // brake to a stop
    {23000, "SWEEP:SPEED:75:0:6000:1"},
    {29500, "BLINKER:RIGHT"},
    {32000, "BLINKER:OFF"},
};
static const int DEMO_STEP_COUNT = sizeof(DEMO_SCRIPT) / sizeof(DemoStep);
static const unsigned long DEMO_CYCLE_MS = 34000;

void demoTick(unsigned long now) {
    if (!demoActive) return;

    unsigned long elapsed = now - demoStartMs;
    while (demoIndex < DEMO_STEP_COUNT && elapsed >= DEMO_SCRIPT[demoIndex].atMs) {
        handleSerialLine(String(DEMO_SCRIPT[demoIndex].cmd));
        demoIndex++;
    }

    if (elapsed >= DEMO_CYCLE_MS) {
        demoStartMs = now;
        demoIndex = 0;
    }
}

// -------- CRC32 (IEEE 802.3, matches JS/zlib crc32) --------

unsigned long crc32buf(const char* data, size_t len) {
    unsigned long crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; i++) {
        crc ^= (unsigned char)data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}
