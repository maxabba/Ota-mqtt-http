#include "ESP32OtaMqtt.h"

// Constructor - creates own WiFiClientSecure and MQTT client
ESP32OtaMqtt::ESP32OtaMqtt(const String& topic)
    : updateTopic(topic), ownsMqttClient(true), ownsWifiClient(true),
      currentStatus(OtaStatus::IDLE), lastCheck(0), retryCount(0),
      statusCallback(nullptr), errorCallback(nullptr), useInsecure(false),
      mqttConnected(false), mqttPort(8883) {

    wifiClient = new WiFiClientSecure();
    mqttClient = new espMqttClient();
    mqttClient->setClient(*wifiClient);
    setupMqttCallbacks();
}

// Constructor - uses existing WiFiClientSecure and MQTT client
ESP32OtaMqtt::ESP32OtaMqtt(WiFiClientSecure& wifi, espMqttClient& mqtt, const String& topic)
    : wifiClient(&wifi), mqttClient(&mqtt), updateTopic(topic),
      ownsMqttClient(false), ownsWifiClient(false),
      currentStatus(OtaStatus::IDLE), lastCheck(0), retryCount(0),
      statusCallback(nullptr), errorCallback(nullptr), useInsecure(false),
      mqttConnected(false), mqttPort(8883) {

    setupMqttCallbacks();
}

// Setup MQTT callbacks
void ESP32OtaMqtt::setupMqttCallbacks() {
    Serial.println("[OTA] Setting up MQTT callbacks...");

    mqttClient->onConnect([this](bool sessionPresent) {
        Serial.println("[OTA] onConnect callback triggered (session: " + String(sessionPresent) + ")");
        onMqttConnect();
    });

    mqttClient->onDisconnect([this](espMqttClientTypes::DisconnectReason reason) {
        Serial.println("[OTA] onDisconnect callback triggered (reason: " + String((int)reason) + ")");
        onMqttDisconnect();
    });

    mqttClient->onMessage([this](const espMqttClientTypes::MessageProperties& properties,
                                  const char* topic, const uint8_t* payload,
                                  size_t len, size_t index, size_t total) {
        Serial.println("[OTA] onMessage callback triggered");
        onMqttMessage(topic, (const char*)payload, len, index, total);
    });

    Serial.println("[OTA] MQTT callbacks configured successfully");
}

// Destructor
ESP32OtaMqtt::~ESP32OtaMqtt() {
    if (mqttClient) {
        mqttClient->disconnect();
    }
    if (ownsMqttClient && mqttClient) {
        delete mqttClient;
    }
    if (ownsWifiClient && wifiClient) {
        delete wifiClient;
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
void ESP32OtaMqtt::setMqttCredentials(const String& user, const String& password) {
    mqttUser = user;
    mqttPassword = password;
    if (mqttClient) {
        mqttClient->setCredentials(user.c_str(), password.c_str());
    }
    Serial.println("[OTA] MQTT credentials configured for user: " + user);
}

void ESP32OtaMqtt::setMqttServer(const String& server, uint16_t port) {
    mqttServer = server;
    mqttPort = port;
    Serial.println("[OTA] MQTT server configured: " + server + ":" + String(port));
}

// SSL/TLS configuration methods
void ESP32OtaMqtt::setCACert(const char* caCert) {
    this->caCert = String(caCert);
    useInsecure = false;

    Serial.println("[OTA] Configuring CA certificate...");
    Serial.println("[OTA] Certificate length: " + String(strlen(caCert)));

    // Apply CA certificate to WiFiClientSecure
    if (wifiClient) {
        wifiClient->setCACert(caCert);
        Serial.println("[OTA] CA certificate applied to WiFiClientSecure");
    } else {
        Serial.println("[OTA] ERROR: wifiClient is NULL, cannot set CA certificate");
    }
    Serial.println("[OTA] CA certificate configured for secure MQTT connection");
}

void ESP32OtaMqtt::setClientCert(const char* clientCert, const char* clientKey) {
    this->clientCert = String(clientCert);
    this->clientKey = String(clientKey);

    // Apply client certificate and key to WiFiClientSecure
    if (wifiClient) {
        wifiClient->setCertificate(clientCert);
        wifiClient->setPrivateKey(clientKey);
    }
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

    if (insecure && wifiClient) {
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

// MQTT connect callback
void ESP32OtaMqtt::onMqttConnect() {
    Serial.println("[OTA] =============================");
    Serial.println("[OTA] MQTT CONNECTION SUCCESSFUL!");
    Serial.println("[OTA] =============================");
    Serial.println("[OTA] Subscribing to topic: " + updateTopic);

    mqttConnected = true;

    mqttClient->subscribe(updateTopic.c_str(), 1);
    Serial.println("[OTA] Successfully subscribed to topic at QoS 1");
    Serial.println("[OTA] Ready to receive OTA update messages");
}

// MQTT disconnect callback
void ESP32OtaMqtt::onMqttDisconnect() {
    Serial.println("[OTA] =============================");
    Serial.println("[OTA] MQTT DISCONNECTED!");
    Serial.println("[OTA] =============================");
    mqttConnected = false;
}

// MQTT message handler
void ESP32OtaMqtt::onMqttMessage(const char* topic, const char* payload,
                                  size_t len, size_t index, size_t total) {
    if (String(topic) != updateTopic) return;
    
    // Handle partial messages
    static String messageBuffer;
    if (index == 0) {
        messageBuffer = "";
    }
    
    for (size_t i = 0; i < len; i++) {
        messageBuffer += (char)payload[i];
    }
    
    // Process complete message
    if (index + len == total) {
        Serial.println("[OTA] Received update message: " + messageBuffer);
        
        if (parseUpdateMessage(messageBuffer)) {
            // Check if this is a newer version
            if (isNewerVersion(pendingVersion, config.currentVersion)) {
                Serial.println("[OTA] New version available: " + pendingVersion);
                updateStatus(OtaStatus::DOWNLOADING);
                
                // Start download in next loop iteration to avoid blocking
            } else {
                Serial.println("[OTA] Version " + pendingVersion + " is not newer than current " + config.currentVersion);
            }
        }
        
        messageBuffer = "";
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
    
    // Verify MQTT server is configured
    if (mqttServer.isEmpty()) {
        reportError("MQTT server not configured. Call setMqttServer() first");
        return false;
    }
    
    Serial.println("[OTA] === MQTT Connection Configuration ===");
    Serial.println("[OTA] Server: " + mqttServer);
    Serial.println("[OTA] Port: " + String(mqttPort));
    Serial.println("[OTA] Username: " + (mqttUser.isEmpty() ? String("None") : mqttUser));
    Serial.println("[OTA] Password: " + (mqttPassword.isEmpty() ? String("None") : String("***")));
    Serial.println("[OTA] CA Cert configured: " + String(!caCert.isEmpty() ? "Yes" : "No"));
    Serial.println("[OTA] Insecure mode: " + String(useInsecure ? "Yes" : "No"));

    if (mqttPort == 8883 || mqttPort == 8884) {
        if (!useInsecure && !caCert.isEmpty()) {
            Serial.println("[OTA] TLS Mode: Secure with CA certificate validation");
        } else if (useInsecure) {
            Serial.println("[OTA] TLS Mode: Insecure (no certificate validation)");
        } else {
            Serial.println("[OTA] TLS Mode: Default (may fail without certificate)");
        }
    }

    // Configure MQTT server and credentials
    Serial.println("[OTA] Configuring MQTT client...");
    mqttClient->setServer(mqttServer.c_str(), mqttPort);

    if (!mqttUser.isEmpty()) {
        mqttClient->setCredentials(mqttUser.c_str(), mqttPassword.c_str());
    }

    // Connect to MQTT (async - connection happens in background)
    Serial.println("[OTA] Initiating MQTT connection...");
    mqttClient->connect();
    Serial.println("[OTA] MQTT connect() called, waiting for callback...");
    
    Serial.println("[OTA] ESP32 OTA MQTT updater initialized");
    Serial.println("[OTA] Current version: " + config.currentVersion);
    Serial.println("[OTA] Update topic: " + updateTopic);
    Serial.println("[OTA] Check interval: " + String(config.checkInterval) + "ms");
    
    return true;
}

// Non-blocking main loop
void ESP32OtaMqtt::loop() {
    if (!WiFi.isConnected()) {
        if (mqttConnected) {
            mqttClient->disconnect();
        }
        return;
    }
    
    // Auto-reconnect MQTT if disconnected
    if (!mqttConnected && mqttServer.length() > 0) {
        static unsigned long lastReconnectAttempt = 0;
        if (millis() - lastReconnectAttempt > 5000) {
            lastReconnectAttempt = millis();
            Serial.println("[OTA] MQTT not connected, attempting reconnection...");
            Serial.println("[OTA] MQTT client status: " + String(mqttClient->connected() ? "connected" : "disconnected"));
            Serial.println("[OTA] Time since last attempt: " + String(millis() - lastReconnectAttempt) + "ms");
            mqttClient->connect();
            Serial.println("[OTA] Reconnect attempt initiated");
        }
    }
    
    // Periodic update check
    if (millis() - lastCheck >= config.checkInterval) {
        lastCheck = millis();
        checkForUpdates();
    }
    
    // Handle pending download
    if (currentStatus == OtaStatus::DOWNLOADING && !pendingUrl.isEmpty()) {
        if (downloadFirmware(pendingUrl, pendingChecksum)) {
            updateStatus(OtaStatus::INSTALLING);
            if (installFirmware()) {
                updateStatus(OtaStatus::SUCCESS);
                config.currentVersion = pendingVersion;
            } else {
                updateStatus(OtaStatus::ERROR);
                if (config.enableRollback) {
                    performRollback();
                }
            }
        } else {
            retryCount++;
            if (retryCount >= config.maxRetries) {
                updateStatus(OtaStatus::ERROR);
                retryCount = 0;
            }
        }
        
        // Clear pending data
        pendingUrl = "";
        pendingChecksum = "";
        pendingVersion = "";
    }
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
bool ESP32OtaMqtt::downloadFirmware(const String& url, const String& expectedChecksum) {
    Serial.println("[OTA] Starting download from: " + url);
    
    // Prepare for OTA
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        reportError("Cannot begin update", Update.getError());
        return false;
    }
    
    // Initialize SHA256 context for checksum calculation
    mbedtls_sha256_context sha256_ctx;
    mbedtls_sha256_init(&sha256_ctx);
    mbedtls_sha256_starts(&sha256_ctx, 0); // 0 = SHA256 (not SHA224)
    
    // Check if URL is HTTP or HTTPS
    bool isHTTPS = url.startsWith("https://");
    bool isHTTP = url.startsWith("http://");
    
    if (!isHTTP && !isHTTPS) {
        reportError("Invalid URL protocol. Must be HTTP or HTTPS");
        mbedtls_sha256_free(&sha256_ctx);
        Update.abort();
        return false;
    }
    
    // Parse URL and extract host, path, and port
    String host = "";
    String path = "";
    int port = isHTTPS ? 443 : 80;
    
    // Parse URL (simple parser for http(s)://host:port/path)
    int protocolEnd = url.indexOf("://");
    if (protocolEnd != -1) {
        int hostStart = protocolEnd + 3;
        int portStart = url.indexOf(':', hostStart);
        int pathStart = url.indexOf('/', hostStart);
        
        if (portStart != -1 && (pathStart == -1 || portStart < pathStart)) {
            // Port specified
            host = url.substring(hostStart, portStart);
            if (pathStart != -1) {
                port = url.substring(portStart + 1, pathStart).toInt();
                path = url.substring(pathStart);
            } else {
                port = url.substring(portStart + 1).toInt();
                path = "/";
            }
        } else if (pathStart != -1) {
            // No port, but path exists
            host = url.substring(hostStart, pathStart);
            path = url.substring(pathStart);
        } else {
            // No port, no path
            host = url.substring(hostStart);
            path = "/";
        }
    }
    
    Serial.println("[OTA] Protocol: " + String(isHTTPS ? "HTTPS" : "HTTP"));
    Serial.println("[OTA] Host: " + host);
    Serial.println("[OTA] Port: " + String(port));
    Serial.println("[OTA] Path: " + path);
    
    size_t written = 0;
    
    // For HTTP, we need to use a regular WiFiClient
    if (isHTTP) {
        Serial.println("[OTA] Using plain HTTP connection");
        WiFiClient httpClient;
        
        if (!httpClient.connect(host.c_str(), port)) {
            reportError("Connection to HTTP server failed");
            mbedtls_sha256_free(&sha256_ctx);
            Update.abort();
            return false;
        }
        
        // Send HTTP request
        httpClient.println("GET " + path + " HTTP/1.1");
        httpClient.println("Host: " + host);
        httpClient.println("Connection: close");
        httpClient.println();
        
        // Read response headers
        unsigned long timeout = millis() + config.downloadTimeout;
        int contentLength = 0;
        bool isChunked = false;
        
        while (httpClient.connected() && millis() < timeout) {
            String line = httpClient.readStringUntil('\n');
            line.trim();
            
            // Check for Content-Length header
            if (line.startsWith("Content-Length:")) {
                contentLength = line.substring(15).toInt();
                Serial.println("[OTA] Content-Length: " + String(contentLength));
            }
            // Check for chunked encoding
            else if (line.indexOf("Transfer-Encoding: chunked") >= 0) {
                isChunked = true;
                Serial.println("[OTA] Transfer-Encoding: chunked");
            }
            // End of headers
            else if (line.length() == 0) {
                break;
            }
        }
        
        // Download and write firmware
        unsigned long lastProgress = 0;
        uint8_t buffer[1024];
        
        while (httpClient.connected() && millis() < timeout) {
            size_t available = httpClient.available();
            if (available) {
                size_t bytesToRead = (available > sizeof(buffer)) ? sizeof(buffer) : available;
                size_t bytesRead = httpClient.readBytes(buffer, bytesToRead);
                
                if (bytesRead > 0) {
                    // Update SHA256 hash with downloaded data
                    mbedtls_sha256_update(&sha256_ctx, buffer, bytesRead);
                    
                    if (Update.write(buffer, bytesRead) != bytesRead) {
                        reportError("Write failed", Update.getError());
                        Update.abort();
                        httpClient.stop();
                        mbedtls_sha256_free(&sha256_ctx);
                        return false;
                    }
                    
                    written += bytesRead;
                    
                    // Update progress
                    if (contentLength > 0) {
                        int progress = (written * 100) / contentLength;
                        if (progress != lastProgress) {
                            updateStatus(OtaStatus::DOWNLOADING, progress);
                            lastProgress = progress;
                        }
                    } else if (written - lastProgress >= 10240) {
                        // Update every 10KB when content length unknown
                        Serial.print(".");
                        lastProgress = written;
                    }
                }
            }
            
            // Check if download is complete
            if (contentLength > 0 && written >= contentLength) {
                break;
            }
            
            yield(); // Keep watchdog happy
        }
        
        httpClient.stop();
        Serial.println();
        Serial.println("[OTA] Downloaded " + String(written) + " bytes via HTTP");
        
        // Handle case where download failed but we need to cleanup
        if (written == 0) {
            mbedtls_sha256_free(&sha256_ctx);
            Update.abort();
        }
        
    } else {
        // HTTPS with dedicated WiFiClientSecure for download
        Serial.println("[OTA] Using HTTPS connection");
        
        // Create dedicated client for firmware download to avoid conflicts with MQTT client
        WiFiClientSecure downloadClient;
        downloadClient.setInsecure();
        Serial.println("[OTA] Using dedicated insecure HTTPS client for firmware download");
        
        if (!downloadClient.connect(host.c_str(), port)) {
            reportError("Connection to HTTPS server failed");
            mbedtls_sha256_free(&sha256_ctx);
            Update.abort();
            return false;
        }
        
        // Send HTTP request
        downloadClient.println("GET " + path + " HTTP/1.1");
        downloadClient.println("Host: " + host);
        downloadClient.println("Connection: close");
        downloadClient.println();
    
        // Read response headers
        unsigned long timeout = millis() + config.downloadTimeout;
        int contentLength = 0;
        
        while (downloadClient.connected() && millis() < timeout) {
            String line = downloadClient.readStringUntil('\n');
            line.trim();
            
            // Check for Content-Length header
            if (line.startsWith("Content-Length:")) {
                contentLength = line.substring(15).toInt();
                Serial.println("[OTA] Content-Length: " + String(contentLength));
            }
            // End of headers
            else if (line.length() == 0) {
                break;
            }
        }
        
        // Download and write firmware
        unsigned long lastProgress = 0;
        uint8_t buffer[1024];
        
        while (downloadClient.connected() && millis() < timeout) {
            size_t available = downloadClient.available();
            if (available) {
                size_t bytesToRead = (available > sizeof(buffer)) ? sizeof(buffer) : available;
                size_t bytesRead = downloadClient.readBytes(buffer, bytesToRead);
                
                if (bytesRead > 0) {
                    // Update SHA256 hash with downloaded data
                    mbedtls_sha256_update(&sha256_ctx, buffer, bytesRead);
                    
                    if (Update.write(buffer, bytesRead) != bytesRead) {
                        reportError("Write failed", Update.getError());
                        Update.abort();
                        downloadClient.stop();
                        mbedtls_sha256_free(&sha256_ctx);
                        return false;
                    }
                    
                    written += bytesRead;
                    
                    // Update progress
                    if (contentLength > 0) {
                        int progress = (written * 100) / contentLength;
                        if (progress != lastProgress) {
                            updateStatus(OtaStatus::DOWNLOADING, progress);
                            lastProgress = progress;
                        }
                    } else if (written - lastProgress >= 10240) {
                        // Update every 10KB when content length unknown
                        Serial.print(".");
                        lastProgress = written;
                    }
                }
            }
            
            // Check if download is complete
            if (contentLength > 0 && written >= contentLength) {
                break;
            }
            
            yield(); // Keep watchdog happy
        }
        
        downloadClient.stop();
        Serial.println();
        Serial.println("[OTA] Downloaded " + String(written) + " bytes via HTTPS");
        
        // Handle case where download failed but we need to cleanup
        if (written == 0) {
            mbedtls_sha256_free(&sha256_ctx);
            Update.abort();
        }
    }
    
    // Finalize SHA256 calculation
    unsigned char hash[32];
    mbedtls_sha256_finish(&sha256_ctx, hash);
    mbedtls_sha256_free(&sha256_ctx);
    
    // Convert hash to hex string
    calculatedChecksum = "";
    for (int i = 0; i < 32; i++) {
        char hex[3];
        sprintf(hex, "%02x", hash[i]);
        calculatedChecksum += hex;
    }
    
    // Finalize the update
    if (written == 0) {
        reportError("No data received from server");
        Update.abort();
        return false;
    }
    
    if (!Update.end(true)) {
        reportError("Update end failed", Update.getError());
        return false;
    }
    
    // Verify checksum if enabled
    if (config.verifyChecksum && !verifyChecksum(expectedChecksum)) {
        reportError("Checksum verification failed");
        Update.abort();  // Critical: abort the corrupted firmware
        return false;
    }
    
    Serial.println("[OTA] Firmware update ready to install");
    return true;
}

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