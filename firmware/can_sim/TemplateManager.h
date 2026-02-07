#pragma once

#include <Arduino.h>
#include "Template.h"

// Template Manager - handles storage and retrieval of templates
class TemplateManager {
public:
    TemplateManager();

    // Initialize LittleFS and load templates
    bool begin();

    // Get the currently active template
    Template* getCurrentTemplate();

    // Get the current template ID
    const char* getCurrentTemplateId();

    // Load a template by ID (switches active template)
    bool loadTemplate(const char* id);

    // Save a template to storage (custom templates only)
    bool saveTemplate(const Template* t);

    // Delete a template (custom templates only)
    bool deleteTemplate(const char* id);

    // List all available templates (prints to Serial)
    void listTemplates();

    // Check if a template exists
    bool templateExists(const char* id);

    // Check if a template is official (built-in)
    bool isOfficialTemplate(const char* id);

    // Export current template as JSON to Serial
    void exportCurrentTemplate();

    // Import template from JSON string
    bool importTemplateFromJson(const char* json, size_t len);

    // Get template count
    int getTemplateCount();

    // Reset to factory defaults
    void factoryReset();

private:
    Template currentTemplate;
    char currentTemplateId[32];
    bool initialized;

    // Save current template ID to persist across reboots
    void saveCurrentTemplateId();

    // Load saved template ID
    void loadCurrentTemplateId();

    // Parse JSON into template struct
    bool parseJsonToTemplate(const char* json, size_t len, Template* t);

    // Convert template to JSON string
    void templateToJson(const Template* t, char* buffer, size_t bufferSize);

    // Get custom template filename
    void getTemplateFilename(const char* id, char* filename, size_t len);
};

// Global template manager instance
extern TemplateManager templateManager;
