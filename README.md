# ESP32 OTA MQTT Library

A high-performance, non-blocking OTA (Over-The-Air) firmware update library for ESP32 that uses MQTT for update notifications and HTTPS for secure firmware downloads.

## ✨ Key Features

- **🚀 Non-blocking**: Never blocks your main application code
- **🔒 Secure**: HTTP/HTTPS downloads with SHA256 checksum verification
- **🔄 Auto-rollback**: Automatic rollback to previous firmware on failure
- **📱 Flexible**: Use existing WiFi/MQTT clients or let the library create them
- **⚡ High Performance**: Configurable check intervals and retry mechanisms
- **🎯 Smart Versioning**: Semantic version comparison (1.2.3 format)
- **📊 Progress Tracking**: Real-time download progress with callbacks
- **🛡️ Production Ready**: Comprehensive error handling and recovery

## 🚀 Quick Start

### Basic Usage

```cpp
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESP32OtaMqtt.h>

WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);
ESP32OtaMqtt otaUpdater(wifiClient, mqttClient, "device/my_esp32/ota");

void setup() {
    // Connect WiFi and MQTT...
    
    // Configure OTA
    OtaConfig config;
    config.currentVersion = "1.0.0";
    config.checkInterval = 30000;  // Check every 30 seconds
    
    otaUpdater.setConfig(config);
    otaUpdater.begin();
}

void loop() {
    otaUpdater.loop();  // Non-blocking OTA handling
    
    // Your application code here - never blocked!
    yourApplicationCode();
}
```

## 📋 Requirements

- **Platform**: ESP32 only
- **Framework**: Arduino/ESP-IDF
- **Dependencies**:
  - `knolleary/PubSubClient` ^2.8
  - `bblanchon/ArduinoJson` ^6.0.0
  - Built-in ESP32 libraries (WiFi, Update, esp_ota_ops)

## 📦 Installation

### PlatformIO

Add to your `platformio.ini`:

```ini
lib_deps = 
    https://github.com/your-username/esp32-ota-mqtt.git
```

### Arduino IDE

1. Download as ZIP
2. Sketch → Include Library → Add .ZIP Library

## 🏗️ Architecture

### Constructor Options

```cpp
// Option 1: Provide existing WiFi client, library creates MQTT client
ESP32OtaMqtt updater(wifiClient, "update/topic");

// Option 2: Provide both existing WiFi and MQTT clients
ESP32OtaMqtt updater(wifiClient, mqttClient, "update/topic");
```

### Configuration

```cpp
OtaConfig config;
config.currentVersion = "1.0.0";        // Current firmware version
config.checkInterval = 30000;           // MQTT check interval (ms)
config.downloadTimeout = 120000;        // Download timeout (ms)
config.maxRetries = 3;                  // Retry attempts on failure
config.enableRollback = true;           // Enable automatic rollback
config.verifyChecksum = true;           // Verify SHA256 checksums

updater.setConfig(config);
```

### SSL/TLS Security Configuration

```cpp
// Method 1: Hardcoded Certificate (simple setup)
updater.setCACert(ca_certificate);

// Method 2: Load from SPIFFS (recommended for production)
updater.setCACertFromFile("/certs/ca.crt");

// Method 3: Client Certificate for mutual TLS
updater.setClientCert(client_cert, client_key);
// Or from SPIFFS:
updater.setClientCertFromFiles("/certs/client.crt", "/certs/client.key");

// Method 4: Insecure connection (NOT recommended for production)
updater.setInsecure(true);
```

#### SPIFFS Certificate Setup

1. Create `data/certs/` folder in your project
2. Place certificate files: `ca.crt`, `client.crt`, `client.key`
3. Add `board_build.filesystem = spiffs` to platformio.ini
4. Upload certificates: `pio run -t uploadfs`

## 📡 MQTT Message Format

Send JSON messages to your configured topic:

```json
{
  "version": "1.2.0",
  "firmware_url": "https://releases.example.com/firmware-v1.2.0.bin",
  "checksum": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
  "command": "update"
}
```

### Message Fields

- **`version`**: Semantic version string (e.g., "1.2.0")
- **`firmware_url`**: HTTP or HTTPS URL to firmware binary
- **`checksum`**: SHA256 hash of the firmware file
- **`command`**: Must be "update" to trigger update

## 🔄 Update Process

1. **MQTT Listening**: Non-blocking check every `checkInterval` ms
2. **Version Check**: Compare incoming version with current using semantic versioning
3. **Download**: HTTP/HTTPS download with progress tracking
4. **Verification**: SHA256 checksum validation
5. **Installation**: Flash new firmware to ESP32 partition
6. **Rollback**: Automatic rollback on installation failure

## 📊 Callbacks & Monitoring

### Status Updates

```cpp
void onOtaStatus(const String& status, int progress) {
    Serial.println("OTA Status: " + status + " (" + String(progress) + "%)");
    
    if (status == "DOWNLOADING") {
        // Update progress bar, LED indicator, etc.
    } else if (status == "SUCCESS") {
        // Update completed, device will restart
        ESP.restart();
    }
}

updater.onStatusUpdate(onOtaStatus);
```

### Error Handling

```cpp
void onOtaError(const String& error, int errorCode) {
    Serial.println("OTA Error: " + error + " (Code: " + String(errorCode) + ")");
    // Handle errors, send alerts, etc.
}

updater.onError(onOtaError);
```

## 🎛️ API Reference

### Core Methods

```cpp
// Initialization
bool begin();                                    // Initialize the updater
void loop();                                     // Non-blocking update handler

// Configuration
void setConfig(const OtaConfig& config);         // Set complete configuration
void setCheckInterval(unsigned long ms);         // Set MQTT check interval
void setDownloadTimeout(unsigned long ms);       // Set download timeout
void setMaxRetries(int retries);                 // Set retry count
void setCurrentVersion(const String& version);   // Set current firmware version

// Control
void checkForUpdates();                          // Manual update check
void forceUpdate(version, url, checksum);        // Force specific update
void reset();                                    // Reset updater state

// Status
OtaStatus getStatus();                           // Get current status enum
String getStatusString();                        // Get status as string
String getCurrentVersion();                      // Get current firmware version
String getPendingVersion();                      // Get pending update version
bool isUpdateInProgress();                       // Check if update is running
```

### Status Enum

```cpp
enum class OtaStatus {
    IDLE,        // Waiting for updates
    CHECKING,    // Checking for new versions
    DOWNLOADING, // Downloading firmware
    INSTALLING,  // Installing firmware
    SUCCESS,     // Update completed successfully
    ERROR,       // Update failed
    ROLLBACK     // Performing rollback
};
```

## ⚡ Performance Features

- **Non-blocking**: Uses state machine, never blocks main loop
- **Configurable timing**: Adjust check intervals for your use case
- **Memory efficient**: Streaming download, minimal RAM usage
- **Retry logic**: Configurable retry attempts with exponential backoff
- **Progress tracking**: Real-time download progress reporting

## 🛡️ Security Features

- **HTTP/HTTPS support**: Both secure and non-secure firmware downloads
- **Checksum verification**: SHA256 integrity checking
- **Automatic rollback**: Recovery from failed installations
- **Version validation**: Prevents downgrade attacks
- **Partition safety**: Uses ESP32 OTA partition management

## 📋 Examples

### Basic Example
Simple setup with existing MQTT client - see `examples/basic_usage/`

### Advanced Example  
Production-ready implementation with error handling - see `examples/advanced_usage/`

## 🔧 Configuration Tips

### Development Setup
```cpp
config.checkInterval = 10000;    // Check every 10 seconds
config.maxRetries = 1;           // Fail fast for testing
config.enableRollback = false;   // Disable for development
```

### Production Setup
```cpp
config.checkInterval = 300000;   // Check every 5 minutes
config.maxRetries = 5;           // More resilient
config.enableRollback = true;    // Always enable in production
config.verifyChecksum = true;    // Critical for security
```

## 🐛 Troubleshooting

### Common Issues

**WiFi disconnection during update**
- Library handles WiFi reconnection automatically
- Downloads will retry based on `maxRetries` setting

**Download failures**
- Check HTTP/HTTPS URL accessibility
- For HTTPS: Verify certificate validity (use `.setInsecure()` for testing only)
- Increase `downloadTimeout` for large files

**Checksum mismatches**
- Verify SHA256 hash calculation
- Ensure firmware file wasn't corrupted during upload

**MQTT connection issues**
- Library auto-reconnects to MQTT broker
- Check broker accessibility and credentials

**Compilation warnings from ESP32 framework**
- Add these build flags to your `platformio.ini` to suppress framework warnings:
```ini
build_flags = 
    -Wno-unused-variable
    -Wno-return-type
    -Wno-missing-field-initializers
    -Wno-sign-compare
    -Wno-unused-parameter
```

## 📝 License

MIT License - see LICENSE file for details.

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes with tests
4. Submit a pull request

## 📞 Support

For issues and questions:
- GitHub Issues: Report bugs and feature requests
- Documentation: Check examples and API reference
- Community: Share experiences and solutions