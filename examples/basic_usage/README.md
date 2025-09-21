# Basic Usage Example

This example demonstrates how to use the ESP32 OTA MQTT library with two certificate configuration methods:
1. **Hardcoded certificate** (simple for testing)
2. **SPIFFS certificate** (recommended for production)

## Configuration

Edit `basic_usage.ino` and choose your certificate method:

```cpp
// Choose ONE option:
#define USE_HARDCODED_CERT    // Option 1: Use hardcoded certificate
// #define USE_SPIFFS_CERT    // Option 2: Load from SPIFFS
```

## Option 1: Hardcoded Certificate (Simple)

**Pros:**
- ✅ Quick to test
- ✅ No extra setup needed
- ✅ Good for development

**Cons:**
- ❌ Certificate in source code
- ❌ Hard to update
- ❌ Not secure for production

**Usage:**
1. Replace the example certificate with your broker's CA certificate
2. Compile and upload: `pio run -t upload`

## Option 2: SPIFFS Certificate (Recommended)

**Pros:**
- ✅ Certificates separate from code
- ✅ Easy to update without recompiling
- ✅ Secure for production
- ✅ No certificates in version control

**Cons:**
- ❌ Requires initial setup
- ❌ Need to upload certificates separately

**Setup:**

### 1. Create project structure:
```
your-project/
├── src/
│   └── basic_usage.ino
├── data/
│   └── certs/
│       └── ca.crt         # Your CA certificate
└── platformio.ini
```

### 2. Add your CA certificate:

**For AWS IoT:**
```bash
mkdir -p data/certs
curl -o data/certs/ca.crt https://www.amazontrust.com/repository/AmazonRootCA1.pem
```

**For other brokers:**
```bash
# Copy your broker's CA certificate
cp /path/to/your/ca-certificate.pem data/certs/ca.crt
```

### 3. Configure platformio.ini:
```ini
[env:esp32]
platform = espressif32
board = esp32dev
framework = arduino

lib_deps = 
    file:///path/to/ESP32-OTA-MQTT

# Enable SPIFFS
board_build.filesystem = spiffs

monitor_speed = 115200
```

### 4. Upload certificates to SPIFFS:
```bash
# Upload certificates
pio run -t uploadfs

# Upload firmware
pio run -t upload

# Monitor output
pio device monitor
```

## WiFi and MQTT Configuration

Update these settings in the code:

```cpp
// WiFi credentials
const char* ssid = "your_wifi_ssid";
const char* password = "your_wifi_password";

// MQTT settings
const char* mqtt_server = "your_mqtt_broker.com";
const int mqtt_port = 8883;  // 8883 for secure, 1883 for insecure
const char* mqtt_user = "your_mqtt_user";
const char* mqtt_password = "your_mqtt_password";

// OTA settings
const String update_topic = "device/esp32_001/ota";
const String current_version = "1.0.0";
```

## Testing the Update

Send this JSON message to your update topic:

```json
{
  "version": "1.1.0",
  "firmware_url": "https://your-server.com/firmware-v1.1.0.bin",
  "checksum": "sha256_hash_of_your_firmware",
  "command": "update"
}
```

## Common Issues

**"SPIFFS Mount Failed!"**
- Add `board_build.filesystem = spiffs` to platformio.ini
- Run `pio run -t uploadfs` before uploading firmware

**"Certificate file not found"**
- Check file exists: `ls data/certs/`
- Verify path matches: `/certs/ca.crt`
- Re-upload: `pio run -t uploadfs`

**"SSL handshake failed"**
- Verify certificate matches your broker
- Check certificate format (must be PEM)
- Try `otaUpdater.setInsecure(true)` for testing

## Security Tips

1. **Never commit certificates to git:**
```bash
echo "data/certs/" >> .gitignore
```

2. **Use environment variables for credentials:**
```cpp
const char* ssid = getenv("WIFI_SSID");
const char* password = getenv("WIFI_PASSWORD");
```

3. **Rotate certificates regularly**
4. **Use client certificates for mutual TLS when possible**