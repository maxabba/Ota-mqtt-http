// Non-blocking helper implementations for ESP32OtaMqtt
// These functions implement task-based, chunked operations to avoid blocking the main loop

#include "ESP32OtaMqtt.h"

// ============================================================================
// YIELD MANAGEMENT
// ============================================================================

void ESP32OtaMqtt::yieldIfNeeded() {
    unsigned long now = millis();
    if (now - lastYield >= config.yieldInterval) {
        lastYield = now;
        yield(); // Allow other tasks to run
        delay(1); // Minimal delay to prevent watchdog timeout
    }
}

// ============================================================================
// NON-BLOCKING MQTT CONNECTION
// ============================================================================

void ESP32OtaMqtt::handleMqttConnection() {
    unsigned long now = millis();

    switch (mqttState) {
        case MqttConnState::DISCONNECTED:
            // Throttle reconnection attempts (every 5 seconds)
            if (now - lastMqttAttempt >= 5000) {
                lastMqttAttempt = now;
                mqttConnectStartTime = now;
                mqttState = MqttConnState::CONNECTING;
                Serial.println("[OTA] Initiating MQTT connection...");
            }
            break;

        case MqttConnState::CONNECTING:
            // Attempt connection with timeout
            if (now - mqttConnectStartTime < config.mqttConnectTimeout) {
                if (attemptMqttConnect()) {
                    mqttState = MqttConnState::CONNECTED;
                    Serial.println("[OTA] MQTT connected successfully");
                } else {
                    // Connection failed, but don't immediately retry
                    mqttState = MqttConnState::FAILED;
                }
            } else {
                // Timeout reached
                Serial.println("[OTA] MQTT connection timeout");
                mqttState = MqttConnState::FAILED;
            }
            break;

        case MqttConnState::CONNECTED:
            // Connection active, just maintain it
            if (!mqttClient->connected()) {
                Serial.println("[OTA] MQTT connection lost");
                mqttState = MqttConnState::DISCONNECTED;
            } else {
                // Process MQTT messages (non-blocking)
                mqttClient->loop();
            }
            break;

        case MqttConnState::FAILED:
            // Wait before retry
            if (now - lastMqttAttempt >= 5000) {
                mqttState = MqttConnState::DISCONNECTED;
            }
            break;
    }

    yieldIfNeeded();
}

bool ESP32OtaMqtt::attemptMqttConnect() {
    String clientId = "OTA_" + WiFi.macAddress();
    bool connected = false;

    // PubSubClient connect() can block for 15 seconds by default
    // We call it once and rely on the timeout mechanism in handleMqttConnection()
    if (mqttUser.length() > 0 && mqttPassword.length() > 0) {
        connected = mqttClient->connect(clientId.c_str(), mqttUser.c_str(), mqttPassword.c_str());
    } else {
        connected = mqttClient->connect(clientId.c_str());
    }

    if (connected) {
        Serial.println("[OTA] MQTT connected, subscribing to: " + updateTopic);
        mqttClient->subscribe(updateTopic.c_str());
        return true;
    } else {
        Serial.println("[OTA] MQTT connection failed, state: " + String(mqttClient->state()));
        return false;
    }
}

// ============================================================================
// NON-BLOCKING FIRMWARE DOWNLOAD
// ============================================================================

void ESP32OtaMqtt::handleDownload() {
    switch (downloadState) {
        case DownloadState::IDLE:
            // Nothing to do
            break;

        case DownloadState::CONNECTING:
            // Connection attempt is in progress (managed by startDownload)
            break;

        case DownloadState::DOWNLOADING:
            // Process chunk by chunk
            if (!processDownloadChunk()) {
                // Download failed or completed
                if (downloadedBytes > 0) {
                    downloadState = DownloadState::VERIFYING;
                } else {
                    downloadState = DownloadState::FAILED;
                }
            }
            break;

        case DownloadState::VERIFYING:
            // Finalize and verify
            if (finalizeDownload(pendingChecksum)) {
                downloadState = DownloadState::COMPLETE;
                Serial.println("[OTA] Download completed successfully");
            } else {
                downloadState = DownloadState::FAILED;
                Serial.println("[OTA] Download verification failed");
            }
            break;

        case DownloadState::COMPLETE:
            // Download done, ready for installation
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
            cleanupDownload();
            downloadState = DownloadState::IDLE;
            break;

        case DownloadState::FAILED:
            // Handle failure
            retryCount++;
            if (retryCount >= config.maxRetries) {
                updateStatus(OtaStatus::ERROR);
                retryCount = 0;
            } else {
                Serial.println("[OTA] Retry " + String(retryCount) + "/" + String(config.maxRetries));
                // Reset for retry
                cleanupDownload();
                downloadState = DownloadState::IDLE;
                updateStatus(OtaStatus::DOWNLOADING);
                // Will restart download in next loop() when pendingUrl is still set
            }
            break;
    }

    yieldIfNeeded();
}

bool ESP32OtaMqtt::startDownload(const String& url) {
    Serial.println("[OTA] Starting non-blocking download from: " + url);

    // Prepare for OTA
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        reportError("Cannot begin update", Update.getError());
        return false;
    }

    // Initialize SHA256 context
    if (!sha256Initialized) {
        mbedtls_sha256_init(&sha256_ctx);
        mbedtls_sha256_starts(&sha256_ctx, 0);
        sha256Initialized = true;
    }

    // Parse URL
    bool isHTTPS = url.startsWith("https://");
    bool isHTTP = url.startsWith("http://");

    if (!isHTTP && !isHTTPS) {
        reportError("Invalid URL protocol");
        cleanupDownload();
        return false;
    }

    // Parse host, port, path
    String host = "";
    String path = "";
    int port = isHTTPS ? 443 : 80;

    int protocolEnd = url.indexOf("://");
    if (protocolEnd != -1) {
        int hostStart = protocolEnd + 3;
        int portStart = url.indexOf(':', hostStart);
        int pathStart = url.indexOf('/', hostStart);

        if (portStart != -1 && (pathStart == -1 || portStart < pathStart)) {
            host = url.substring(hostStart, portStart);
            if (pathStart != -1) {
                port = url.substring(portStart + 1, pathStart).toInt();
                path = url.substring(pathStart);
            } else {
                port = url.substring(portStart + 1).toInt();
                path = "/";
            }
        } else if (pathStart != -1) {
            host = url.substring(hostStart, pathStart);
            path = url.substring(pathStart);
        } else {
            host = url.substring(hostStart);
            path = "/";
        }
    }

    Serial.println("[OTA] Protocol: " + String(isHTTPS ? "HTTPS" : "HTTP"));
    Serial.println("[OTA] Host: " + host + ":" + String(port));
    Serial.println("[OTA] Path: " + path);

    // Create download client
    if (isHTTP) {
        downloadClient = new WiFiClient();
    } else {
        WiFiClientSecure* secureClient = new WiFiClientSecure();
        secureClient->setInsecure(); // Use dedicated insecure client for download
        downloadClient = secureClient;
    }

    // Connect to server (this may block briefly, but unavoidable with WiFiClient)
    Serial.println("[OTA] Connecting to server...");
    if (!downloadClient->connect(host.c_str(), port)) {
        reportError("Connection failed");
        cleanupDownload();
        return false;
    }

    // Send HTTP request
    downloadClient->println("GET " + path + " HTTP/1.1");
    downloadClient->println("Host: " + host);
    downloadClient->println("Connection: close");
    downloadClient->println();

    // Read headers (quickly, non-blocking)
    unsigned long headerStart = millis();
    totalBytes = 0;

    while (downloadClient->connected() && millis() - headerStart < 5000) {
        if (downloadClient->available()) {
            String line = downloadClient->readStringUntil('\n');
            line.trim();

            if (line.startsWith("Content-Length:")) {
                totalBytes = line.substring(15).toInt();
                Serial.println("[OTA] Content-Length: " + String(totalBytes));
            }

            if (line.length() == 0) {
                break; // End of headers
            }
        }
        yield();
    }

    downloadedBytes = 0;
    downloadStartTime = millis();
    downloadState = DownloadState::DOWNLOADING;
    Serial.println("[OTA] Starting chunked download...");

    return true;
}

bool ESP32OtaMqtt::processDownloadChunk() {
    // Check timeout
    if (millis() - downloadStartTime > config.downloadTimeout) {
        reportError("Download timeout");
        cleanupDownload();
        return false;
    }

    // Check if data available
    if (!downloadClient || !downloadClient->connected()) {
        // Connection closed, download complete or failed
        return false;
    }

    size_t available = downloadClient->available();
    if (available == 0) {
        // No data yet, check if still connected
        yieldIfNeeded();
        return true; // Continue waiting
    }

    // Read chunk (configurable size, default 512 bytes)
    uint8_t buffer[1024];
    size_t bytesToRead = min(available, min(config.chunkSize, sizeof(buffer)));
    size_t bytesRead = downloadClient->readBytes(buffer, bytesToRead);

    if (bytesRead > 0) {
        // Update SHA256
        mbedtls_sha256_update(&sha256_ctx, buffer, bytesRead);

        // Write to flash
        if (Update.write(buffer, bytesRead) != bytesRead) {
            reportError("Flash write failed", Update.getError());
            cleanupDownload();
            return false;
        }

        downloadedBytes += bytesRead;

        // Report progress
        if (totalBytes > 0) {
            int progress = (downloadedBytes * 100) / totalBytes;
            updateStatus(OtaStatus::DOWNLOADING, progress);
        }

        // Yield after each chunk
        yieldIfNeeded();
    }

    // Check if download complete
    if (totalBytes > 0 && downloadedBytes >= totalBytes) {
        return false; // Signal completion
    }

    return true; // Continue downloading
}

bool ESP32OtaMqtt::finalizeDownload(const String& expectedChecksum) {
    Serial.println("[OTA] Finalizing download: " + String(downloadedBytes) + " bytes");

    if (downloadedBytes == 0) {
        reportError("No data received");
        cleanupDownload();
        Update.abort();
        return false;
    }

    // Finalize SHA256
    unsigned char hash[32];
    mbedtls_sha256_finish(&sha256_ctx, hash);

    calculatedChecksum = "";
    for (int i = 0; i < 32; i++) {
        char hex[3];
        sprintf(hex, "%02x", hash[i]);
        calculatedChecksum += hex;
    }

    Serial.println("[OTA] Calculated checksum: " + calculatedChecksum);

    // End update
    if (!Update.end(true)) {
        reportError("Update end failed", Update.getError());
        cleanupDownload();
        return false;
    }

    // Verify checksum
    if (config.verifyChecksum && !verifyChecksum(expectedChecksum)) {
        reportError("Checksum mismatch");
        cleanupDownload();
        Update.abort();
        return false;
    }

    Serial.println("[OTA] Download verified successfully");
    return true;
}

void ESP32OtaMqtt::cleanupDownload() {
    if (downloadClient) {
        downloadClient->stop();
        delete downloadClient;
        downloadClient = nullptr;
    }

    if (sha256Initialized) {
        mbedtls_sha256_free(&sha256_ctx);
        sha256Initialized = false;
    }

    downloadState = DownloadState::IDLE;
    downloadedBytes = 0;
    totalBytes = 0;
}
