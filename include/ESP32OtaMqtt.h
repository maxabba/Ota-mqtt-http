#ifndef ESP32_OTA_MQTT_H
#define ESP32_OTA_MQTT_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <PubSubClient.h>
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

// MQTT connection state for non-blocking connection
enum class MqttConnState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    FAILED
};

// Download state for chunked non-blocking download
enum class DownloadState {
    IDLE,
    CONNECTING,
    DOWNLOADING,
    VERIFYING,
    COMPLETE,
    FAILED
};

// Configuration structure
struct OtaConfig {
    unsigned long checkInterval = 30000;    // 30 seconds default
    unsigned long downloadTimeout = 60000;  // 60 seconds default
    int maxRetries = 3;                     // 3 retries default
    bool enableRollback = true;             // Enable automatic rollback
    bool verifyChecksum = true;             // Verify SHA256 checksum
    String currentVersion = "1.0.0";        // Current firmware version
    size_t chunkSize = 512;                 // Download chunk size (bytes per loop iteration)
    unsigned long yieldInterval = 50;       // Yield every N ms during operations
    unsigned long mqttConnectTimeout = 15000; // MQTT connect timeout (ms)
};

class ESP32OtaMqtt {
private:
    // Core components
    WiFiClientSecure* wifiClient;
    PubSubClient* mqttClient;
    bool ownsMqttClient;
    bool ownsWifiClient;
    
    // Configuration
    String updateTopic;
    OtaConfig config;
    String mqttServer;
    int mqttPort;
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

    // Non-blocking MQTT connection state
    MqttConnState mqttState;
    unsigned long mqttConnectStartTime;
    unsigned long lastMqttAttempt;

    // Non-blocking download state
    DownloadState downloadState;
    WiFiClient* downloadClient;
    unsigned long downloadStartTime;
    unsigned long lastYield;
    size_t totalBytes;
    size_t downloadedBytes;
    mbedtls_sha256_context sha256_ctx;
    bool sha256Initialized;
    
    // Callbacks
    OtaStatusCallback statusCallback;
    OtaErrorCallback errorCallback;
    
    // Internal methods
    void mqttCallback(char* topic, byte* payload, unsigned int length);
    static void staticMqttCallback(char* topic, byte* payload, unsigned int length);
    bool parseUpdateMessage(const String& message);
    String extractJsonValue(const String& json, const String& key);
    bool isNewerVersion(const String& newVersion, const String& currentVersion);
    int compareVersions(const String& v1, const String& v2);

    // Non-blocking MQTT management
    void handleMqttConnection();
    bool attemptMqttConnect();

    // Non-blocking download management
    void handleDownload();
    bool startDownload(const String& url);
    bool processDownloadChunk();
    bool finalizeDownload(const String& expectedChecksum);
    void cleanupDownload();

    bool installFirmware();
    bool verifyChecksum(const String& expectedChecksum);
    void performRollback();
    void updateStatus(OtaStatus status, int progress = 0);
    void reportError(const String& error, int errorCode = 0);
    void yieldIfNeeded();
    
    // Static instance for callback
    static ESP32OtaMqtt* instance;
    
public:
    // Constructors
    ESP32OtaMqtt(const String& topic);  // Simple constructor, creates own clients
    ESP32OtaMqtt(WiFiClientSecure& wifi, const String& topic);
    ESP32OtaMqtt(WiFiClientSecure& wifi, PubSubClient& mqtt, const String& topic);
    
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
    void setMqttServer(const char* server, int port = 8883);
    void setMqttCredentials(const String& user, const String& password);
    
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