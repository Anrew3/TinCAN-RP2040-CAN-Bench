#pragma once

// TinCAN Firmware Version
#define TINCAN_VERSION "2.0.0"

// CAN Bus Settings
#define CS_PIN 19
#define CAN_SPEED CAN_500KBPS
#define CAN_CLOCK MCP_16MHZ

// Serial Settings
#define SERIAL_BAUD 9600

// Template Settings
#define MAX_TEMPLATES 10
#define MAX_TEMPLATE_SIZE 4096
#define MAX_CUSTOM_CAN 10
#define MAX_BUTTONS 10
#define MAX_GAUGES 6
#define MAX_TEMP_SENSORS 4
#define MAX_TIRES 4
#define MAX_BACKGROUND_MSGS 5

// Global logging control (defined in can_sim.ino)
extern bool g_verboseSerial;
extern unsigned long g_logMinIntervalMs;

// Logging helper
bool canLog(unsigned long now);
