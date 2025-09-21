#include <WiFi.h>
#include <WiFiClientSecure.h>
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
const String device_id = "esp32_secure_001";
const String update_topic = "devices/" + device_id + "/ota";
const String current_version = "1.0.0";

// CA Certificate (Root CA that signed your broker's certificate)
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

// Client Certificate (if using mutual TLS authentication)
const char* client_cert = \
"-----BEGIN CERTIFICATE-----\n" \
"Your client certificate here...\n" \
"-----END CERTIFICATE-----\n";

// Client Private Key (if using mutual TLS authentication)
const char* client_key = \
"-----BEGIN PRIVATE KEY-----\n" \
"Your client private key here...\n" \
"-----END PRIVATE KEY-----\n";

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

void setup() {
    Serial.begin(115200);
    Serial.println("ESP32 Secure OTA MQTT Example Starting...");
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
    
    // Configure SSL/TLS certificates
    Serial.println("Configuring SSL/TLS certificates...");
    
    // Method 1: Use CA certificate for server verification (most common)
    otaUpdater.setCACert(ca_cert);
    Serial.println("CA certificate configured");
    
    // Method 2: Use client certificate for mutual TLS (uncomment if needed)
    // otaUpdater.setClientCert(client_cert, client_key);
    // Serial.println("Client certificate and key configured");
    
    // Method 3: Use insecure connection (NOT recommended for production)
    // otaUpdater.setInsecure(true);
    // Serial.println("WARNING: Using insecure connection");
    
    // Configure OTA updater
    OtaConfig config;
    config.currentVersion = current_version;
    config.checkInterval = 60000;      // Check every minute for demo
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
        Serial.println("Using SSL/TLS: YES");
    } else {
        Serial.println("Failed to initialize OTA updater!");
    }
    
    Serial.println();
    Serial.println("=== Security Configuration ===");
    Serial.println("- MQTTS (port 8883) with CA certificate verification");
    Serial.println("- Secure HTTPS firmware downloads");
    Serial.println("- SHA256 checksum verification");
    Serial.println("- Automatic rollback on failure");
    Serial.println();
    
    Serial.println("To trigger an update, publish to: " + update_topic);
    Serial.println("Example secure update message:");
    Serial.println("{");
    Serial.println("  \"version\": \"1.1.0\",");
    Serial.println("  \"firmware_url\": \"https://secure.example.com/firmware-v1.1.0.bin\",");
    Serial.println("  \"checksum\": \"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\",");
    Serial.println("  \"command\": \"update\"");
    Serial.println("}");
}

void loop() {
    // Handle secure OTA updates
    otaUpdater.loop();
    
    // Your application code here
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 30000) { // Every 30 seconds
        lastHeartbeat = millis();
        
        Serial.println("[APP] System Status:");
        Serial.println("  Version: " + otaUpdater.getCurrentVersion());
        Serial.println("  Status: " + otaUpdater.getStatusString());
        Serial.println("  Uptime: " + String(millis() / 1000) + " seconds");
        Serial.println("  Free heap: " + String(ESP.getFreeHeap()) + " bytes");
        Serial.println("  WiFi RSSI: " + String(WiFi.RSSI()) + " dBm");
    }
    
    delay(100);
}