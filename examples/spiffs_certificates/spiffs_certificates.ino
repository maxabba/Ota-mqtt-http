#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SPIFFS.h>
#include <ESP32OtaMqtt.h>

// WiFi credentials
const char* ssid = "your_wifi_ssid";
const char* password = "your_wifi_password";

// Secure MQTT settings
const char* mqtt_server = "your_secure_broker.com";
const int mqtt_port = 8883;  // MQTTS port
const char* mqtt_user = "your_mqtt_user";
const char* mqtt_password = "your_mqtt_password";

// Device settings
const String device_id = "esp32_spiffs_001";
const String update_topic = "devices/" + device_id + "/ota";
const String current_version = "1.0.0";

// Certificate file paths in SPIFFS
const String ca_cert_path = "/certs/ca.crt";
const String client_cert_path = "/certs/client.crt";
const String client_key_path = "/certs/client.key";

// Create WiFi client and OTA updater
WiFiClientSecure wifiSecureClient;
ESP32OtaMqtt otaUpdater(wifiSecureClient, update_topic);

void onOtaStatus(const String& status, int progress) {
    Serial.println("[OTA] Status: " + status + " (" + String(progress) + "%)");
    
    if (status == "SUCCESS") {
        Serial.println("[OTA] Update completed successfully! Restarting...");
        delay(2000);
        ESP.restart();
    }
}

void onOtaError(const String& error, int errorCode) {
    Serial.println("[OTA] Error: " + error + " (Code: " + String(errorCode) + ")");
}

void listSPIFFSFiles() {
    Serial.println("=== SPIFFS File System ===");
    
    File root = SPIFFS.open("/");
    if (!root) {
        Serial.println("Failed to open root directory");
        return;
    }
    
    if (!root.isDirectory()) {
        Serial.println("Root is not a directory");
        return;
    }
    
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.print("DIR: ");
            Serial.println(file.name());
        } else {
            Serial.print("FILE: ");
            Serial.print(file.name());
            Serial.print(" (");
            Serial.print(file.size());
            Serial.println(" bytes)");
        }
        file = root.openNextFile();
    }
    Serial.println("==========================");
}

bool checkCertificateFiles() {
    Serial.println("Checking certificate files...");
    
    bool allFilesExist = true;
    
    // Check CA certificate
    if (SPIFFS.exists(ca_cert_path)) {
        File file = SPIFFS.open(ca_cert_path, "r");
        Serial.println("✓ CA Certificate: " + ca_cert_path + " (" + String(file.size()) + " bytes)");
        file.close();
    } else {
        Serial.println("✗ CA Certificate missing: " + ca_cert_path);
        allFilesExist = false;
    }
    
    // Check client certificate (optional)
    if (SPIFFS.exists(client_cert_path)) {
        File file = SPIFFS.open(client_cert_path, "r");
        Serial.println("✓ Client Certificate: " + client_cert_path + " (" + String(file.size()) + " bytes)");
        file.close();
    } else {
        Serial.println("ℹ Client Certificate not found: " + client_cert_path + " (optional)");
    }
    
    // Check client key (optional)
    if (SPIFFS.exists(client_key_path)) {
        File file = SPIFFS.open(client_key_path, "r");
        Serial.println("✓ Client Key: " + client_key_path + " (" + String(file.size()) + " bytes)");
        file.close();
    } else {
        Serial.println("ℹ Client Key not found: " + client_key_path + " (optional)");
    }
    
    return allFilesExist;
}

void setup() {
    Serial.begin(115200);
    Serial.println("ESP32 OTA MQTT with SPIFFS Certificates Example");
    Serial.println("Device ID: " + device_id);
    Serial.println("Current Version: " + current_version);
    
    // Initialize SPIFFS
    Serial.println("Initializing SPIFFS...");
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS initialization failed!");
        Serial.println("Make sure to upload certificate files to SPIFFS partition");
        while (1) delay(1000);
    }
    
    Serial.println("SPIFFS initialized successfully");
    Serial.printf("Total space: %zu bytes\n", SPIFFS.totalBytes());
    Serial.printf("Used space: %zu bytes\n", SPIFFS.usedBytes());
    
    // List files in SPIFFS
    listSPIFFSFiles();
    
    // Check certificate files
    if (!checkCertificateFiles()) {
        Serial.println("");
        Serial.println("=== Certificate Upload Instructions ===");
        Serial.println("1. Create a 'data' folder in your project root");
        Serial.println("2. Create 'data/certs/' subdirectory");
        Serial.println("3. Place your certificate files:");
        Serial.println("   - data/certs/ca.crt (CA certificate)");
        Serial.println("   - data/certs/client.crt (client cert, optional)");
        Serial.println("   - data/certs/client.key (client key, optional)");
        Serial.println("4. Upload to SPIFFS: pio run -t uploadfs");
        Serial.println("5. Reset the device");
        Serial.println("======================================");
        while (1) delay(1000);
    }
    
    // Connect to WiFi
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    Serial.println("WiFi connected! IP: " + WiFi.localIP().toString());
    
    // Configure SSL/TLS certificates from SPIFFS
    Serial.println("Loading certificates from SPIFFS...");
    
    // Load CA certificate (required for server verification)
    otaUpdater.setCACertFromFile(ca_cert_path);
    
    // Load client certificate and key (optional for mutual TLS)
    if (SPIFFS.exists(client_cert_path) && SPIFFS.exists(client_key_path)) {
        otaUpdater.setClientCertFromFiles(client_cert_path, client_key_path);
        Serial.println("Client certificate authentication enabled");
    } else {
        Serial.println("Client certificate authentication disabled (files not found)");
    }
    
    // Configure OTA updater
    OtaConfig config;
    config.currentVersion = current_version;
    config.checkInterval = 60000;      // Check every minute
    config.downloadTimeout = 300000;   // 5 minute timeout
    config.maxRetries = 3;
    config.enableRollback = true;
    config.verifyChecksum = true;
    
    otaUpdater.setConfig(config);
    otaUpdater.onStatusUpdate(onOtaStatus);
    otaUpdater.onError(onOtaError);
    
    // Initialize OTA updater
    if (otaUpdater.begin()) {
        Serial.println("Secure OTA updater initialized successfully");
        Serial.println("Update topic: " + update_topic);
        Serial.println("MQTT server: " + String(mqtt_server) + ":" + String(mqtt_port));
        Serial.println("Certificates loaded from SPIFFS");
    } else {
        Serial.println("Failed to initialize OTA updater!");
    }
    
    Serial.println();
    Serial.println("=== Ready for secure OTA updates ===");
}

void loop() {
    // Handle secure OTA updates
    otaUpdater.loop();
    
    // Your application code here
    static unsigned long lastStatus = 0;
    if (millis() - lastStatus > 30000) { // Every 30 seconds
        lastStatus = millis();
        
        Serial.println("[APP] System Status:");
        Serial.println("  Version: " + otaUpdater.getCurrentVersion());
        Serial.println("  Status: " + otaUpdater.getStatusString());
        Serial.println("  Uptime: " + String(millis() / 1000) + " seconds");
        Serial.println("  Free heap: " + String(ESP.getFreeHeap()) + " bytes");
        Serial.println("  SPIFFS free: " + String(SPIFFS.totalBytes() - SPIFFS.usedBytes()) + " bytes");
    }
    
    delay(100);
}