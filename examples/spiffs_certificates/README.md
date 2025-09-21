# SPIFFS Certificates Example

This example shows how to load SSL/TLS certificates from SPIFFS filesystem instead of hardcoding them in your source code.

## Advantages of SPIFFS Certificates

- ✅ **Separation of code and certificates**: Certificates are stored separately from source code
- ✅ **Security**: No certificates in version control or binary
- ✅ **Flexibility**: Easy to update certificates without recompiling
- ✅ **Multiple environments**: Different certificates for dev/staging/production

## Setup Instructions

### 1. Prepare Certificate Files

Create a `data` folder in your project root:

```
your-project/
├── src/
│   └── main.cpp
├── data/
│   └── certs/
│       ├── ca.crt          # CA certificate (required)
│       ├── client.crt      # Client certificate (optional)
│       └── client.key      # Client private key (optional)
└── platformio.ini
```

### 2. Get Your Certificates

**For AWS IoT Core:**
```bash
# Download Amazon Root CA 1
curl -o data/certs/ca.crt https://www.amazontrust.com/repository/AmazonRootCA1.pem

# Your device certificate and key from AWS IoT Console
cp your-device.cert.pem data/certs/client.crt
cp your-device.private.key data/certs/client.key
```

**For other MQTT brokers:**
- Contact your MQTT broker provider for CA certificate
- Use `openssl` to extract CA from server certificate:
```bash
openssl s_client -showcerts -connect your-broker.com:8883 </dev/null 2>/dev/null | openssl x509 -outform PEM > data/certs/ca.crt
```

### 3. Configure PlatformIO

Add to your `platformio.ini`:

```ini
[env:esp32doit-devkit-v1]
platform = espressif32
board = esp32doit-devkit-v1
framework = arduino

lib_deps = 
    file:///path/to/ESP32-OTA-MQTT

# Enable SPIFFS upload
board_build.filesystem = spiffs

monitor_speed = 115200
```

### 4. Upload Certificates to SPIFFS

```bash
# Upload files in data/ folder to SPIFFS partition
pio run -t uploadfs

# Or with Arduino IDE: Tools > ESP32 Sketch Data Upload
```

### 5. Upload and Run

```bash
# Upload firmware
pio run -t upload

# Monitor serial output
pio device monitor
```

## Certificate File Formats

All certificate files should be in PEM format:

**CA Certificate (ca.crt):**
```
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
...
-----END CERTIFICATE-----
```

**Client Certificate (client.crt):**
```
-----BEGIN CERTIFICATE-----
MIIC9TCCAd2gAwIBAgIJAK8l7QjF9nD4MA0GCSqGSIb3DQEBCwUAMIGJMQswCQYD
...
-----END CERTIFICATE-----
```

**Client Private Key (client.key):**
```
-----BEGIN PRIVATE KEY-----
MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQC7S2uyBLw3Dz9k
...
-----END PRIVATE KEY-----
```

## Security Best Practices

1. **Never commit certificates to version control**
   ```bash
   echo "data/certs/" >> .gitignore
   ```

2. **Use environment-specific certificates**
   ```
   data/
   ├── certs-dev/
   ├── certs-staging/
   └── certs-production/
   ```

3. **Validate certificate files before upload**
   ```bash
   # Verify certificate
   openssl x509 -in data/certs/ca.crt -text -noout
   
   # Verify private key
   openssl rsa -in data/certs/client.key -check
   ```

4. **Monitor certificate expiration**
   ```bash
   # Check expiration date
   openssl x509 -in data/certs/client.crt -noout -dates
   ```

## Troubleshooting

**"SPIFFS initialization failed"**
- Make sure `board_build.filesystem = spiffs` is in platformio.ini
- Verify SPIFFS partition is available in partition table

**"Failed to open CA certificate file"**
- Check file exists: `pio run -t uploadfs`
- Verify file path and name exactly match
- Check SPIFFS has enough space

**"SSL handshake failed"**
- Verify CA certificate matches your MQTT broker
- Check certificate is in PEM format
- Ensure certificate is not expired

**"Client authentication failed"**
- Verify client certificate and key match
- Check if broker requires client certificates
- Ensure private key is not encrypted