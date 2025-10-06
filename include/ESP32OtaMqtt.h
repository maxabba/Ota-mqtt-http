#ifndef ESP32_OTA_MQTT_H
#define ESP32_OTA_MQTT_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <AsyncMqttClient.h>
#include <SPIFFS.h>
#include <mbedtls/sha256.h>

// Callback function types
typedef void (*OtaStatusCallback)(const String& status, int progress);
typedef void (*OtaErrorCallback)(const String& error, int errorCode);

// Update status enum
enum class OtaStatus {
    IDLE,
    CHECKING,
    DOWNLOADING,
    INSTALLING,
    SUCCESS,
    ERROR,
    ROLLBACK
};

// Configuration structure
struct OtaConfig {
    unsigned long checkInterval = 30000;    // 30 seconds default
    unsigned long downloadTimeout = 60000;  // 60 seconds default
    int maxRetries = 3;                     // 3 retries default
    bool enableRollback = true;             // Enable automatic rollback
    bool verifyChecksum = true;             // Verify SHA256 checksum
    String currentVersion = "1.0.0";        // Current firmware version
};

class ESP32OtaMqtt {
private:
    // Core components
    WiFiClientSecure* wifiClient;
    AsyncMqttClient* mqttClient;
    bool ownsMqttClient;
    
    // Configuration
    String updateTopic;
    OtaConfig config;
    String mqttUser;
    String mqttPassword;
    
    // SSL/TLS configuration
    String caCert;
    String clientCert;
    String clientKey;
    bool useInsecure;
    
    // State management
    OtaStatus currentStatus;
    unsigned long lastCheck;
    String pendingVersion;
    String pendingUrl;
    String pendingChecksum;
    int retryCount;
    String calculatedChecksum;
    
    // Callbacks
    OtaStatusCallback statusCallback;
    OtaErrorCallback errorCallback;
    
    // Internal methods
    void setupMqttCallbacks();
    void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);
    void onMqttConnect(bool sessionPresent);
    void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
    bool parseUpdateMessage(const String& message);
    String extractJsonValue(const String& json, const String& key);
    bool isNewerVersion(const String& newVersion, const String& currentVersion);
    int compareVersions(const String& v1, const String& v2);
    bool downloadFirmware(const String& url, const String& expectedChecksum);
    bool installFirmware();
    bool verifyChecksum(const String& expectedChecksum);
    void performRollback();
    void updateStatus(OtaStatus status, int progress = 0);
    void reportError(const String& error, int errorCode = 0);
    
    // MQTT connection state
    bool mqttConnected;
    String mqttServer;
    uint16_t mqttPort;
    
public:
    // Constructors
    ESP32OtaMqtt(WiFiClientSecure& wifi, const String& topic);
    ESP32OtaMqtt(WiFiClientSecure& wifi, AsyncMqttClient& mqtt, const String& topic);
    
    // Destructor
    ~ESP32OtaMqtt();
    
    // Configuration methods
    void setConfig(const OtaConfig& newConfig);
    OtaConfig getConfig() const;
    void setCheckInterval(unsigned long intervalMs);
    void setDownloadTimeout(unsigned long timeoutMs);
    void setMaxRetries(int retries);
    void setCurrentVersion(const String& version);
    
    // MQTT configuration methods
    void setMqttCredentials(const String& user, const String& password);
    void setMqttServer(const String& server, uint16_t port);
    
    // SSL/TLS configuration methods
    void setCACert(const char* caCert);
    void setCACertFromFile(const String& caCertPath);
    void setClientCert(const char* clientCert, const char* clientKey);
    void setClientCertFromFiles(const String& clientCertPath, const String& clientKeyPath);
    void setInsecure(bool insecure = true);
    
    // Callback registration
    void onStatusUpdate(OtaStatusCallback callback);
    void onError(OtaErrorCallback callback);
    
    // Control methods
    bool begin();
    void loop();
    void checkForUpdates();
    void forceUpdate(const String& version, const String& url, const String& checksum);
    
    // Status methods
    OtaStatus getStatus() const;
    String getStatusString() const;
    String getCurrentVersion() const;
    String getPendingVersion() const;
    unsigned long getLastCheck() const;
    
    // Utility methods
    void reset();
    bool isUpdateInProgress() const;
};

#endif