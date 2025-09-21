#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESP32OtaMqtt.h>
#include <SPIFFS.h>

// ====================
// CONFIGURATION SECTION
// ====================

// Choose certificate method (comment/uncomment one)
#define USE_HARDCODED_CERT    // Option 1: Use hardcoded certificate (simple for testing)
// #define USE_SPIFFS_CERT    // Option 2: Load from SPIFFS (recommended for production)

// WiFi credentials
const char* ssid = "your_wifi_ssid";
const char* password = "your_wifi_password";

// MQTT settings
const char* mqtt_server = "your_mqtt_broker.com";
const int mqtt_port = 8883;  // Port 8883 for MQTTS (secure)
const char* mqtt_user = "your_mqtt_user";
const char* mqtt_password = "your_mqtt_password";

#ifdef USE_HARDCODED_CERT
// CA Certificate for MQTT broker (example - Amazon Root CA 1)
// Replace with your broker's CA certificate
const char* ca_cert = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n" \
"ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n" \
"b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n" \
"MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n" \
"b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n" \
"ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n" \
"9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n" \
"IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n" \
"VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n" \
"93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n" \
"jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n" \
"AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n" \
"A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n" \
"U5PMCCjjmCXPI6T53iHTfIuJruydjsw2hUwsOBNjsv0pkOuNmYVcQc+c9QCTC8ez\n" \
"Cff5YBnkZNZQCfIkHTHB4FjBBvbynJoMIcI/nOTBVHZNXFCkC0jfX0W8Y4EcVPb2\n" \
"Mwx6G/JlTwTxWNkR8CfHBTXTv+NQxXo/Nx8TdcCKxwWfX8K4FIZqU4HpE7mBBKTh\n" \
"oXNr1+fZuI8c7jW7Z8w8wXvnGq4QBdQQ4IwI4Q3L3BSn4z+Agg0J9/j4sZUIaBMF\n" \
"8SLjCkgaqxMLqNjyUIQoMT0xGNTJMA==\n" \
"-----END CERTIFICATE-----\n";
#endif

#ifdef USE_SPIFFS_CERT
// Certificate file path in SPIFFS
const String ca_cert_path = "/certs/ca.crt";
#endif

// OTA settings
const String update_topic = "device/esp32_001/ota";
const String current_version = "1.0.0";

// Create WiFi client and MQTT client
WiFiClientSecure wifiSecureClient;
PubSubClient mqttClient(wifiSecureClient);

// Create OTA updater (using existing MQTT client)
ESP32OtaMqtt otaUpdater(wifiSecureClient, mqttClient, update_topic);

// Callback functions for OTA status
void onOtaStatus(const String& status, int progress) {
    Serial.println("[APP] OTA Status: " + status + " (" + String(progress) + "%)");
    
    // You can add LED indicators, display updates, etc. here
    if (status == "DOWNLOADING") {
        Serial.println("[APP] Download progress: " + String(progress) + "%");
    } else if (status == "SUCCESS") {
        Serial.println("[APP] Update completed successfully! Restarting...");
        delay(2000);
        ESP.restart();
    }
}

void onOtaError(const String& error, int errorCode) {
    Serial.println("[APP] OTA Error: " + error + " (Code: " + String(errorCode) + ")");
    
    // Handle errors (LED blink, send alert, etc.)
    // The library will automatically retry based on configuration
}

// Regular MQTT callback for your application
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    
    Serial.println("[APP] Received: " + String(topic) + " -> " + message);
    
    // Handle your application's MQTT messages here
    // The OTA updater handles its own messages automatically
}

void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println("====================================");
    Serial.println("ESP32 OTA MQTT Library - Basic Usage");
    Serial.println("====================================");
    
    #ifdef USE_HARDCODED_CERT
        Serial.println("Mode: HARDCODED CERTIFICATE");
    #endif
    #ifdef USE_SPIFFS_CERT
        Serial.println("Mode: SPIFFS CERTIFICATE");
    #endif
    
    Serial.println("Version: " + current_version);
    Serial.println("Device: esp32_001");
    Serial.println("====================================");
    Serial.println();
    
    // Connect to WiFi
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    Serial.println("WiFi connected! IP: " + WiFi.localIP().toString());
    
    // Configure SSL/TLS for secure MQTT connection
    Serial.println();
    Serial.println("Configuring SSL/TLS...");
    
    #ifdef USE_HARDCODED_CERT
        Serial.println("Using hardcoded CA certificate");
        otaUpdater.setCACert(ca_cert);
    #endif
    
    #ifdef USE_SPIFFS_CERT
        Serial.println("Loading CA certificate from SPIFFS...");
        
        // Initialize SPIFFS
        if (!SPIFFS.begin(true)) {
            Serial.println("SPIFFS Mount Failed!");
            Serial.println("Please upload certificates with: pio run -t uploadfs");
            while(1) delay(1000);
        }
        
        // Check if certificate file exists
        if (!SPIFFS.exists(ca_cert_path)) {
            Serial.println("Certificate file not found: " + ca_cert_path);
            Serial.println("Instructions:");
            Serial.println("1. Create 'data/certs/' folder in project");
            Serial.println("2. Copy your CA certificate to 'data/certs/ca.crt'");
            Serial.println("3. Run: pio run -t uploadfs");
            while(1) delay(1000);
        }
        
        // Load certificate from SPIFFS
        otaUpdater.setCACertFromFile(ca_cert_path);
        Serial.println("CA certificate loaded from SPIFFS successfully");
    #endif
    
    // Optional: For testing only - use insecure connection
    // otaUpdater.setInsecure(true);  // WARNING: Not secure!
    
    // Configure MQTT
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setCallback(mqttCallback);
    
    // Configure OTA updater
    OtaConfig config;
    config.currentVersion = current_version;
    config.checkInterval = 30000;      // Check every 30 seconds
    config.downloadTimeout = 120000;   // 2 minute download timeout
    config.maxRetries = 3;             // Retry 3 times on failure
    config.enableRollback = true;      // Enable automatic rollback
    config.verifyChecksum = true;      // Verify SHA256 checksum
    
    otaUpdater.setConfig(config);
    otaUpdater.onStatusUpdate(onOtaStatus);
    otaUpdater.onError(onOtaError);
    
    // Set MQTT credentials if required by your broker
    otaUpdater.setMqttCredentials(mqtt_user, mqtt_password);
    
    // Initialize OTA updater
    if (otaUpdater.begin()) {
        Serial.println("OTA updater initialized successfully");
    } else {
        Serial.println("Failed to initialize OTA updater");
    }
    
    Serial.println("Setup completed. Current firmware version: " + current_version);
    Serial.println("Listening for updates on topic: " + update_topic);
    Serial.println();
    Serial.println("To trigger an update, send a JSON message like:");
    Serial.println("{");
    Serial.println("  \"version\": \"1.1.0\",");
    Serial.println("  \"firmware_url\": \"https://example.com/firmware.bin\",");
    Serial.println("  \"checksum\": \"sha256_hash_here\",");
    Serial.println("  \"command\": \"update\"");
    Serial.println("}");
}

void loop() {
    // Handle OTA updates (non-blocking)
    otaUpdater.loop();
    
    // Your application code here
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 60000) { // Every minute
        lastHeartbeat = millis();
        
        if (mqttClient.connected()) {
            String heartbeat = "{\"device\":\"esp32_001\",\"version\":\"" + 
                             otaUpdater.getCurrentVersion() + 
                             "\",\"uptime\":" + String(millis()) + 
                             ",\"status\":\"" + otaUpdater.getStatusString() + "\"}";
            mqttClient.publish("device/esp32_001/heartbeat", heartbeat.c_str());
            Serial.println("[APP] Heartbeat sent");
        }
    }
    
    // Handle your other application tasks here
    // The OTA updater runs independently and won't block your code
    
    delay(100); // Small delay to prevent watchdog issues
}