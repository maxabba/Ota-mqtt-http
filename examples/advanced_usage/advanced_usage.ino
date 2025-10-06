#include <WiFi.h>
#include <ESP32OtaMqtt.h>

// WiFi credentials
const char* ssid = "your_wifi_ssid";
const char* password = "your_wifi_password";

// MQTT settings
const char* mqtt_server = "your_mqtt_broker.com";
const int mqtt_port = 8883; // Secure MQTT port
const char* mqtt_user = "your_mqtt_user";
const char* mqtt_password = "your_mqtt_password";

// Device settings
const String device_id = "esp32_advanced_001";
const String update_topic = "devices/" + device_id + "/ota";
const String current_version = "2.1.0";

// Create OTA updater (PsychicMqttClient handles TLS internally)
ESP32OtaMqtt otaUpdater(update_topic);

// Status tracking
bool updateInProgress = false;
unsigned long updateStartTime = 0;

// Advanced OTA status callback with detailed logging
void onOtaStatus(const String& status, int progress) {
    Serial.println("[OTA] Status Update: " + status + " (" + String(progress) + "%)");
    
    if (status == "DOWNLOADING") {
        if (!updateInProgress) {
            updateInProgress = true;
            updateStartTime = millis();
            Serial.println("[OTA] Starting firmware download...");
            
            // Disable non-critical operations during update
            // e.g., stop sensors, pause data collection, etc.
        }
        
        // Update progress indicator (LED, display, etc.)
        if (progress % 10 == 0) { // Log every 10%
            Serial.println("[OTA] Download progress: " + String(progress) + "%");
        }
        
    } else if (status == "INSTALLING") {
        Serial.println("[OTA] Installing firmware...");
        
    } else if (status == "SUCCESS") {
        unsigned long updateDuration = millis() - updateStartTime;
        Serial.println("[OTA] Update completed successfully in " + String(updateDuration) + "ms");
        Serial.println("[OTA] New version: " + otaUpdater.getPendingVersion());
        Serial.println("[OTA] Restarting in 3 seconds...");
        
        // Save any critical data before restart
        // e.g., configuration, sensor calibration, etc.
        
        delay(3000);
        ESP.restart();
        
    } else if (status == "ERROR") {
        updateInProgress = false;
        Serial.println("[OTA] Update failed, resuming normal operation");
        
    } else if (status == "ROLLBACK") {
        Serial.println("[OTA] Performing rollback to previous firmware");
        
    } else if (status == "IDLE") {
        updateInProgress = false;
    }
}

// Advanced error callback with recovery strategies
void onOtaError(const String& error, int errorCode) {
    Serial.println("[OTA] Error occurred: " + error + " (Code: " + String(errorCode) + ")");
    updateInProgress = false;
    
    // Implement recovery strategies based on error type
    if (error.indexOf("Connection") != -1) {
        Serial.println("[OTA] Network error detected, will retry automatically");
        // Network errors are handled by retry mechanism
        
    } else if (error.indexOf("Checksum") != -1) {
        Serial.println("[OTA] Checksum verification failed, firmware may be corrupted");
        // Could implement reporting back to server
        
    } else if (error.indexOf("JSON") != -1) {
        Serial.println("[OTA] Invalid update message format");
        // Could request message resend
        
    } else {
        Serial.println("[OTA] Unknown error, manual intervention may be required");
    }
    
    // Log error to EEPROM or send to monitoring system
    // logErrorToStorage(error, errorCode);
}

void setup() {
    Serial.begin(115200);
    Serial.println("ESP32 Advanced OTA MQTT Example Starting...");
    Serial.println("Device ID: " + device_id);
    Serial.println("Current Version: " + current_version);
    
    // Connect to WiFi
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    Serial.println("WiFi connected! IP: " + WiFi.localIP().toString());
    Serial.println("MAC Address: " + WiFi.macAddress());
    
    // Configure advanced OTA settings
    OtaConfig config;
    config.currentVersion = current_version;
    config.checkInterval = 45000;      // Check every 45 seconds
    config.downloadTimeout = 300000;   // 5 minute download timeout for large files
    config.maxRetries = 5;             // More retries for unstable connections
    config.enableRollback = true;      // Critical for production
    config.verifyChecksum = true;      // Always verify in production
    
    otaUpdater.setConfig(config);
    
    // Register callbacks
    otaUpdater.onStatusUpdate(onOtaStatus);
    otaUpdater.onError(onOtaError);
    
    // Initialize OTA updater
    if (otaUpdater.begin()) {
        Serial.println("[OTA] Advanced OTA updater initialized successfully");
        Serial.println("[OTA] Update topic: " + update_topic);
        Serial.println("[OTA] Check interval: " + String(config.checkInterval) + "ms");
        Serial.println("[OTA] Download timeout: " + String(config.downloadTimeout) + "ms");
        Serial.println("[OTA] Max retries: " + String(config.maxRetries));
    } else {
        Serial.println("[OTA] Failed to initialize OTA updater!");
    }
    
    Serial.println();
    Serial.println("=== Advanced OTA Features ===");
    Serial.println("- Non-blocking operation");
    Serial.println("- Automatic rollback on failure");
    Serial.println("- SHA256 checksum verification");
    Serial.println("- Configurable retry attempts");
    Serial.println("- Detailed progress tracking");
    Serial.println("- Semantic version comparison");
    Serial.println();
    
    Serial.println("To trigger an update, publish to: " + update_topic);
    Serial.println("Example message:");
    Serial.println("{");
    Serial.println("  \"version\": \"2.2.0\",");
    Serial.println("  \"firmware_url\": \"https://releases.example.com/firmware-v2.2.0.bin\",");
    Serial.println("  \"checksum\": \"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\",");
    Serial.println("  \"command\": \"update\"");
    Serial.println("}");
}

void loop() {
    // OTA updater handles everything non-blocking
    otaUpdater.loop();
    
    // Simulate your application workload
    static unsigned long lastWork = 0;
    if (millis() - lastWork > 5000) { // Every 5 seconds
        lastWork = millis();
        
        if (!updateInProgress) {
            // Normal application operations
            Serial.println("[APP] Performing normal work... (Version: " + 
                         otaUpdater.getCurrentVersion() + 
                         ", Status: " + otaUpdater.getStatusString() + ")");
            
            // Your sensors, data processing, communication, etc. go here
            // The OTA process won't interfere with these operations
            
        } else {
            Serial.println("[APP] Update in progress, reduced functionality mode");
            // Minimal operations during update
        }
    }
    
    // Force update example (uncomment to test)
    // static bool forceUpdateTriggered = false;
    // if (millis() > 30000 && !forceUpdateTriggered) { // After 30 seconds
    //     forceUpdateTriggered = true;
    //     Serial.println("[APP] Triggering force update for testing...");
    //     otaUpdater.forceUpdate("2.1.1", "https://example.com/test-firmware.bin", "test_checksum");
    // }
    
    // Status reporting
    static unsigned long lastStatusReport = 0;
    if (millis() - lastStatusReport > 120000) { // Every 2 minutes
        lastStatusReport = millis();
        
        Serial.println("[APP] === System Status ===");
        Serial.println("[APP] Uptime: " + String(millis() / 1000) + " seconds");
        Serial.println("[APP] Free heap: " + String(ESP.getFreeHeap()) + " bytes");
        Serial.println("[APP] WiFi RSSI: " + String(WiFi.RSSI()) + " dBm");
        Serial.println("[APP] OTA Status: " + otaUpdater.getStatusString());
        Serial.println("[APP] Current Version: " + otaUpdater.getCurrentVersion());
        Serial.println("[APP] Last Check: " + String((millis() - otaUpdater.getLastCheck()) / 1000) + " seconds ago");
        Serial.println("[APP] =====================");
    }
    
    delay(50); // Small delay for stability
}