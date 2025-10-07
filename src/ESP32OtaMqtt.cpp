#include "ESP32OtaMqtt.h"

// Static instance for callback
ESP32OtaMqtt* ESP32OtaMqtt::instance = nullptr;

// Simple constructor - creates own WiFiClientSecure and PubSubClient
ESP32OtaMqtt::ESP32OtaMqtt(const String& topic)
    : updateTopic(topic), ownsMqttClient(true), ownsWifiClient(true),
      currentStatus(OtaStatus::IDLE), lastCheck(0), retryCount(0),
      statusCallback(nullptr), errorCallback(nullptr), useInsecure(false),
      mqttState(MqttConnState::DISCONNECTED), mqttConnectStartTime(0), lastMqttAttempt(0),
      downloadState(DownloadState::IDLE), downloadClient(nullptr), downloadStartTime(0),
      lastYield(0), totalBytes(0), downloadedBytes(0), sha256Initialized(false),
      mqttPort(8883) {

    wifiClient = new WiFiClientSecure();
    mqttClient = new PubSubClient(*wifiClient);
    instance = this;
}

// Constructor with existing WiFi only
ESP32OtaMqtt::ESP32OtaMqtt(WiFiClientSecure& wifi, const String& topic)
    : wifiClient(&wifi), updateTopic(topic), ownsMqttClient(true), ownsWifiClient(false),
      currentStatus(OtaStatus::IDLE), lastCheck(0), retryCount(0),
      statusCallback(nullptr), errorCallback(nullptr), useInsecure(false),
      mqttState(MqttConnState::DISCONNECTED), mqttConnectStartTime(0), lastMqttAttempt(0),
      downloadState(DownloadState::IDLE), downloadClient(nullptr), downloadStartTime(0),
      lastYield(0), totalBytes(0), downloadedBytes(0), sha256Initialized(false),
      mqttPort(8883) {

    mqttClient = new PubSubClient(*wifiClient);
    instance = this;
}

// Constructor with existing WiFi and MQTT
ESP32OtaMqtt::ESP32OtaMqtt(WiFiClientSecure& wifi, PubSubClient& mqtt, const String& topic)
    : wifiClient(&wifi), mqttClient(&mqtt), updateTopic(topic), ownsMqttClient(false), ownsWifiClient(false),
      currentStatus(OtaStatus::IDLE), lastCheck(0), retryCount(0),
      statusCallback(nullptr), errorCallback(nullptr), useInsecure(false),
      mqttState(MqttConnState::DISCONNECTED), mqttConnectStartTime(0), lastMqttAttempt(0),
      downloadState(DownloadState::IDLE), downloadClient(nullptr), downloadStartTime(0),
      lastYield(0), totalBytes(0), downloadedBytes(0), sha256Initialized(false),
      mqttPort(8883) {

    instance = this;
}

// Destructor
ESP32OtaMqtt::~ESP32OtaMqtt() {
    cleanupDownload();
    if (ownsMqttClient && mqttClient) {
        delete mqttClient;
    }
    if (ownsWifiClient && wifiClient) {
        delete wifiClient;
    }
    if (instance == this) {
        instance = nullptr;
    }
}

// Configuration methods
void ESP32OtaMqtt::setConfig(const OtaConfig& newConfig) {
    config = newConfig;
}

OtaConfig ESP32OtaMqtt::getConfig() const {
    return config;
}

void ESP32OtaMqtt::setCheckInterval(unsigned long intervalMs) {
    config.checkInterval = intervalMs;
}

void ESP32OtaMqtt::setDownloadTimeout(unsigned long timeoutMs) {
    config.downloadTimeout = timeoutMs;
}

void ESP32OtaMqtt::setMaxRetries(int retries) {
    config.maxRetries = retries;
}

void ESP32OtaMqtt::setCurrentVersion(const String& version) {
    config.currentVersion = version;
}

// MQTT configuration methods
void ESP32OtaMqtt::setMqttServer(const char* server, int port) {
    mqttServer = String(server);
    mqttPort = port;
    mqttClient->setServer(server, port);
    Serial.println("[OTA] MQTT server configured: " + mqttServer + ":" + String(mqttPort));
}

void ESP32OtaMqtt::setMqttCredentials(const String& user, const String& password) {
    mqttUser = user;
    mqttPassword = password;
    Serial.println("[OTA] MQTT credentials configured for user: " + user);
}

// SSL/TLS configuration methods
void ESP32OtaMqtt::setCACert(const char* caCert) {
    this->caCert = String(caCert);
    useInsecure = false;
    
    // Apply CA certificate to WiFiClientSecure using stored string
    wifiClient->setCACert(this->caCert.c_str());
    Serial.println("[OTA] CA certificate configured for secure MQTT connection");
}

void ESP32OtaMqtt::setClientCert(const char* clientCert, const char* clientKey) {
    this->clientCert = String(clientCert);
    this->clientKey = String(clientKey);
    
    // Apply client certificate and key
    wifiClient->setCertificate(clientCert);
    wifiClient->setPrivateKey(clientKey);
    Serial.println("[OTA] Client certificate and key configured");
}

void ESP32OtaMqtt::setCACertFromFile(const String& caCertPath) {
    if (!SPIFFS.begin(true)) {
        reportError("Failed to mount SPIFFS");
        return;
    }
    
    File file = SPIFFS.open(caCertPath, "r");
    if (!file) {
        reportError("Failed to open CA certificate file: " + caCertPath);
        return;
    }
    
    String cert = file.readString();
    file.close();
    
    if (cert.length() == 0) {
        reportError("CA certificate file is empty: " + caCertPath);
        return;
    }
    
    // Debug: Print certificate info
    Serial.println("[OTA] Certificate file size: " + String(cert.length()) + " bytes");
    Serial.println("[OTA] Certificate starts with: " + cert.substring(0, min(50, (int)cert.length())));
    Serial.println("[OTA] Certificate ends with: " + cert.substring(max(0, (int)cert.length()-50)));
    
    // Validate certificate format
    if (!cert.startsWith("-----BEGIN CERTIFICATE-----")) {
        reportError("Invalid certificate format - missing BEGIN CERTIFICATE header");
        return;
    }
    
    if (!cert.endsWith("-----END CERTIFICATE-----") && !cert.endsWith("-----END CERTIFICATE-----\n")) {
        reportError("Invalid certificate format - missing END CERTIFICATE footer");
        return;
    }
    
    setCACert(cert.c_str());
    Serial.println("[OTA] CA certificate loaded from SPIFFS: " + caCertPath);
}

void ESP32OtaMqtt::setClientCertFromFiles(const String& clientCertPath, const String& clientKeyPath) {
    if (!SPIFFS.begin(true)) {
        reportError("Failed to mount SPIFFS");
        return;
    }
    
    // Read client certificate
    File certFile = SPIFFS.open(clientCertPath, "r");
    if (!certFile) {
        reportError("Failed to open client certificate file: " + clientCertPath);
        return;
    }
    
    String cert = certFile.readString();
    certFile.close();
    
    // Read client key
    File keyFile = SPIFFS.open(clientKeyPath, "r");
    if (!keyFile) {
        reportError("Failed to open client key file: " + clientKeyPath);
        return;
    }
    
    String key = keyFile.readString();
    keyFile.close();
    
    if (cert.length() == 0 || key.length() == 0) {
        reportError("Client certificate or key file is empty");
        return;
    }
    
    setClientCert(cert.c_str(), key.c_str());
    Serial.println("[OTA] Client certificate and key loaded from SPIFFS");
}

void ESP32OtaMqtt::setInsecure(bool insecure) {
    useInsecure = insecure;
    
    if (insecure) {
        wifiClient->setInsecure();
        Serial.println("[OTA] WARNING: Using insecure connection (certificates not verified)");
    } else {
        Serial.println("[OTA] Secure connection enabled");
    }
}

// Callback registration
void ESP32OtaMqtt::onStatusUpdate(OtaStatusCallback callback) {
    statusCallback = callback;
}

void ESP32OtaMqtt::onError(OtaErrorCallback callback) {
    errorCallback = callback;
}

// Semantic version comparison
bool ESP32OtaMqtt::isNewerVersion(const String& newVersion, const String& currentVersion) {
    return compareVersions(newVersion, currentVersion) > 0;
}

int ESP32OtaMqtt::compareVersions(const String& v1, const String& v2) {
    if (v1 == v2) return 0;
    
    // Parse version strings (e.g., "1.2.3")
    int v1Parts[3] = {0, 0, 0};
    int v2Parts[3] = {0, 0, 0};
    
    // Parse v1
    int partIndex = 0;
    String temp = "";
    for (int i = 0; i < v1.length() && partIndex < 3; i++) {
        char c = v1.charAt(i);
        if (c == '.') {
            v1Parts[partIndex++] = temp.toInt();
            temp = "";
        } else if (isDigit(c)) {
            temp += c;
        }
    }
    if (partIndex < 3 && temp.length() > 0) {
        v1Parts[partIndex] = temp.toInt();
    }
    
    // Parse v2
    partIndex = 0;
    temp = "";
    for (int i = 0; i < v2.length() && partIndex < 3; i++) {
        char c = v2.charAt(i);
        if (c == '.') {
            v2Parts[partIndex++] = temp.toInt();
            temp = "";
        } else if (isDigit(c)) {
            temp += c;
        }
    }
    if (partIndex < 3 && temp.length() > 0) {
        v2Parts[partIndex] = temp.toInt();
    }
    
    // Compare parts
    for (int i = 0; i < 3; i++) {
        if (v1Parts[i] > v2Parts[i]) return 1;
        if (v1Parts[i] < v2Parts[i]) return -1;
    }
    
    return 0;
}

// Static MQTT callback wrapper
void ESP32OtaMqtt::staticMqttCallback(char* topic, byte* payload, unsigned int length) {
    if (instance) {
        instance->mqttCallback(topic, payload, length);
    }
}

// MQTT message handler
void ESP32OtaMqtt::mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (String(topic) != updateTopic) return;
    
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    
    Serial.println("[OTA] Received update message: " + message);
    
    if (parseUpdateMessage(message)) {
        // Check if this is a newer version
        if (isNewerVersion(pendingVersion, config.currentVersion)) {
            Serial.println("[OTA] New version available: " + pendingVersion);
            updateStatus(OtaStatus::DOWNLOADING);
            
            // Start download in next loop iteration to avoid blocking MQTT
            // The actual download will be handled in loop()
        } else {
            Serial.println("[OTA] Version " + pendingVersion + " is not newer than current " + config.currentVersion);
        }
    }
}

// Simple JSON value extractor
String ESP32OtaMqtt::extractJsonValue(const String& json, const String& key) {
    String searchKey = "\"" + key + "\"";
    int keyIndex = json.indexOf(searchKey);
    if (keyIndex == -1) return "";
    
    int colonIndex = json.indexOf(":", keyIndex);
    if (colonIndex == -1) return "";
    
    int startIndex = json.indexOf("\"", colonIndex);
    if (startIndex == -1) return "";
    startIndex++; // Skip opening quote
    
    int endIndex = json.indexOf("\"", startIndex);
    if (endIndex == -1) return "";
    
    return json.substring(startIndex, endIndex);
}

// Parse JSON update message
bool ESP32OtaMqtt::parseUpdateMessage(const String& message) {
    // Extract required fields using simple parser
    String version = extractJsonValue(message, "version");
    String url = extractJsonValue(message, "firmware_url");
    String checksum = extractJsonValue(message, "checksum");
    String command = extractJsonValue(message, "command");
    
    if (version.isEmpty() || url.isEmpty() || checksum.isEmpty() || command.isEmpty()) {
        reportError("Missing required fields in update message");
        return false;
    }
    
    if (command != "update") {
        Serial.println("[OTA] Ignoring non-update command: " + command);
        return false;
    }
    
    pendingVersion = version;
    pendingUrl = url;
    pendingChecksum = checksum;
    
    return true;
}

// Initialize the OTA updater
bool ESP32OtaMqtt::begin() {
    if (!WiFi.isConnected()) {
        reportError("WiFi not connected");
        return false;
    }
    
    // Set up MQTT callback
    mqttClient->setCallback(staticMqttCallback);
    
    Serial.println("[OTA] ESP32 OTA MQTT updater initialized");
    Serial.println("[OTA] Current version: " + config.currentVersion);
    Serial.println("[OTA] Update topic: " + updateTopic);
    Serial.println("[OTA] Check interval: " + String(config.checkInterval) + "ms");
    
    return true;
}

// Non-blocking main loop with task-based management
void ESP32OtaMqtt::loop() {
    if (!WiFi.isConnected()) return;

    // Task 1: Handle MQTT connection (non-blocking state machine)
    handleMqttConnection();

    // Task 2: Periodic update check
    if (millis() - lastCheck >= config.checkInterval) {
        lastCheck = millis();
        checkForUpdates();
    }

    // Task 3: Handle download (chunked, non-blocking)
    if (currentStatus == OtaStatus::DOWNLOADING) {
        if (downloadState == DownloadState::IDLE && !pendingUrl.isEmpty()) {
            // Start new download
            if (startDownload(pendingUrl)) {
                downloadState = DownloadState::DOWNLOADING;
            } else {
                // Failed to start
                retryCount++;
                if (retryCount >= config.maxRetries) {
                    updateStatus(OtaStatus::ERROR);
                    retryCount = 0;
                    pendingUrl = "";
                    pendingChecksum = "";
                    pendingVersion = "";
                }
            }
        } else if (downloadState != DownloadState::IDLE) {
            // Continue download
            handleDownload();

            // Check if download finished (success or failure)
            if (downloadState == DownloadState::IDLE) {
                // Clear pending data after completion
                pendingUrl = "";
                pendingChecksum = "";
                pendingVersion = "";
            }
        }
    }

    // Yield to prevent watchdog timeout
    yieldIfNeeded();
}

// Check for updates (called periodically)
void ESP32OtaMqtt::checkForUpdates() {
    if (currentStatus != OtaStatus::IDLE) return;
    
    updateStatus(OtaStatus::CHECKING);
    // The actual check happens via MQTT callback
    // This just updates the status
    updateStatus(OtaStatus::IDLE);
}

// Force a specific update
void ESP32OtaMqtt::forceUpdate(const String& version, const String& url, const String& checksum) {
    if (currentStatus != OtaStatus::IDLE) {
        reportError("Update already in progress");
        return;
    }
    
    pendingVersion = version;
    pendingUrl = url;
    pendingChecksum = checksum;
    retryCount = 0;
    
    updateStatus(OtaStatus::DOWNLOADING);
}

// Download firmware with progress tracking

// Install firmware
bool ESP32OtaMqtt::installFirmware() {
    Serial.println("[OTA] Installing firmware...");
    
    // The firmware is already written by Update.write() in downloadFirmware()
    // Update.end() should have completed the installation
    
    if (Update.hasError()) {
        reportError("Installation failed", Update.getError());
        return false;
    }
    
    Serial.println("[OTA] Installation completed successfully");
    return true;
}

// Verify SHA256 checksum
bool ESP32OtaMqtt::verifyChecksum(const String& expectedChecksum) {
    Serial.println("[OTA] Expected checksum: " + expectedChecksum);
    Serial.println("[OTA] Calculated checksum: " + calculatedChecksum);
    
    // Convert both checksums to lowercase for comparison
    String expectedLower = expectedChecksum;
    String calculatedLower = calculatedChecksum;
    expectedLower.toLowerCase();
    calculatedLower.toLowerCase();
    
    bool isValid = (expectedLower == calculatedLower);
    
    if (isValid) {
        Serial.println("[OTA] Checksum verification: PASSED");
    } else {
        Serial.println("[OTA] Checksum verification: FAILED");
    }
    
    return isValid;
}

// Perform rollback to previous firmware
void ESP32OtaMqtt::performRollback() {
    Serial.println("[OTA] Rollback requested...");
    updateStatus(OtaStatus::ROLLBACK);
    
    // Simple rollback strategy - just restart and hope for the best
    // In a production implementation, you would use ESP32 partition management
    reportError("Manual rollback required - restart device to previous firmware");
    Serial.println("[OTA] Restarting device...");
    delay(2000);
    ESP.restart();
}

// Update status and notify callback
void ESP32OtaMqtt::updateStatus(OtaStatus status, int progress) {
    currentStatus = status;
    
    if (statusCallback) {
        statusCallback(getStatusString(), progress);
    }
    
    Serial.println("[OTA] Status: " + getStatusString() + " (" + String(progress) + "%)");
}

// Report error and notify callback
void ESP32OtaMqtt::reportError(const String& error, int errorCode) {
    Serial.println("[OTA] Error: " + error + " (Code: " + String(errorCode) + ")");
    
    if (errorCallback) {
        errorCallback(error, errorCode);
    }
    
    currentStatus = OtaStatus::ERROR;
}

// Status methods
OtaStatus ESP32OtaMqtt::getStatus() const {
    return currentStatus;
}

String ESP32OtaMqtt::getStatusString() const {
    switch (currentStatus) {
        case OtaStatus::IDLE: return "IDLE";
        case OtaStatus::CHECKING: return "CHECKING";
        case OtaStatus::DOWNLOADING: return "DOWNLOADING";
        case OtaStatus::INSTALLING: return "INSTALLING";
        case OtaStatus::SUCCESS: return "SUCCESS";
        case OtaStatus::ERROR: return "ERROR";
        case OtaStatus::ROLLBACK: return "ROLLBACK";
        default: return "UNKNOWN";
    }
}

String ESP32OtaMqtt::getCurrentVersion() const {
    return config.currentVersion;
}

String ESP32OtaMqtt::getPendingVersion() const {
    return pendingVersion;
}

unsigned long ESP32OtaMqtt::getLastCheck() const {
    return lastCheck;
}

// Reset the updater
void ESP32OtaMqtt::reset() {
    currentStatus = OtaStatus::IDLE;
    pendingVersion = "";
    pendingUrl = "";
    pendingChecksum = "";
    retryCount = 0;
}

// Check if update is in progress
bool ESP32OtaMqtt::isUpdateInProgress() const {
    return currentStatus == OtaStatus::DOWNLOADING || 
           currentStatus == OtaStatus::INSTALLING ||
           currentStatus == OtaStatus::ROLLBACK;
}