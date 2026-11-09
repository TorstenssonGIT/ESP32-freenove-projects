/*
 * =============================================================================
 *  Lab 8 - Step 1: Scan nearby 2.4 GHz Wi-Fi networks (ESP32-S3)
 * =============================================================================
 *
 *  What it does
 *  ------------
 *  Every 15 seconds the ESP32-S3 scans all 2.4 GHz channels and reports
 *  what it finds:
 *    - LCD    : scan status, number of networks, then one network at a time
 *               (SSID on line 1, number / RSSI / channel on line 2).
 *    - Serial : one machine-readable line per network (used by the Python
 *               smoke-test), including the security mode.
 *
 *  The scan only LISTENS. It never joins any network, not even open ones.
 *
 *  Hardware (unchanged from earlier labs)
 *  --------------------------------------
 *    LCD1602 + PCF8574T I2C adapter : SDA = GPIO 1, SCL = GPIO 2, addr 0x27
 *    Yellow status LED              : GPIO 13  (on while scanning)
 *
 *  Serial protocol (do not change - the smoke-test parses it)
 *  ---------------------------------------------------------
 *    SCAN_BEGIN
 *    SCAN_RESULT count=<n>
 *    AP index=<i> ssid_hex=<hex> rssi=<dBm> channel=<ch> auth=<mode>
 *    SCAN_END count=<n>
 *  On failure:
 *    SCAN_ERROR code=<code>
 *    SCAN_END count=0 error=1
 *
 *  The SSID is printed as hex so that spaces, symbols, and non-ASCII
 *  characters can never break the line format.
 * =============================================================================
 */

#include <Arduino.h>
#include <LiquidCrystal_PCF8574.h>
#include <WiFi.h>
#include <Wire.h>

// Local-only SSID/password (KWifiSsid, KWifiPassword). Not used by the scan,
// which never joins a network; included so later Lab 8 steps share one
// setup. Create it by copying include/wifi_credentials.example.h.
// The build stops here if the file is missing - that is intentional.
#include "wifi_credentials.h"

// -----------------------------------------------------------------------------
//  Pin and hardware settings
// -----------------------------------------------------------------------------
const uint8_t KPinLcdSda    = 1;     // I2C data line to the LCD adapter
const uint8_t KPinLcdScl    = 2;     // I2C clock line to the LCD adapter
const uint8_t KPinStatusLed = 13;    // Yellow LED, lit while a scan runs
const uint8_t KLcdAddress   = 0x27;  // PCF8574T default I2C address

const uint8_t KLcdColumns = 16;
const uint8_t KLcdRows    = 2;

// -----------------------------------------------------------------------------
//  Timing settings
// -----------------------------------------------------------------------------
const uint32_t KScanIntervalMillis = 15000;  // Time between scan starts
const uint32_t KLcdCountMillis     = 1000;   // How long "Networks found" shows
const uint32_t KLcdResultMillis    = 1200;   // How long each network shows

// -----------------------------------------------------------------------------
//  Global state
// -----------------------------------------------------------------------------
LiquidCrystal_PCF8574 lcd(KLcdAddress);

uint32_t lastScanMillis = 0;     // When the previous scan finished
bool     firstScan      = true;  // Scan immediately after boot


// =============================================================================
//  LCD helpers
// =============================================================================

// Clear the LCD and write up to two lines of text.
void showOnLcd(const char* line1, const char* line2 = "") {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(line1);
    lcd.setCursor(0, 1);
    lcd.print(line2);
}

// Build a 16-character LCD line from an SSID.
//   - Hidden networks (empty SSID) are shown as "(hidden SSID)".
//   - Long names are cut to 16 characters.
//   - Characters the LCD cannot show (outside printable ASCII) become '?'.
//   - The rest of the line is padded with spaces.
// 'output' must hold at least KLcdColumns + 1 bytes.
void formatSsidForLcd(const String& ssid, char* output) {
    const String label = ssid.isEmpty() ? String("(hidden SSID)") : ssid;

    for (uint8_t column = 0; column < KLcdColumns; ++column) {
        if (column < label.length()) {
            const uint8_t character = static_cast<uint8_t>(label[column]);
            const bool isPrintable = character >= 32 && character <= 126;
            output[column] = isPrintable ? static_cast<char>(character) : '?';
        } else {
            output[column] = ' ';
        }
    }
    output[KLcdColumns] = '\0';
}

// Show one network on the LCD, e.g.
//   line 1: "MyHomeNetwork   "
//   line 2: "3 -67dBm CH6"
void showNetworkOnLcd(int16_t index, const String& ssid) {
    char ssidLine[KLcdColumns + 1];
    formatSsidForLcd(ssid, ssidLine);

    char detailLine[KLcdColumns + 1];
    snprintf(detailLine, sizeof(detailLine), "%d %ddBm CH%d",
             index + 1,                       // 1-based for people
             static_cast<int>(WiFi.RSSI(index)),
             static_cast<int>(WiFi.channel(index)));

    showOnLcd(ssidLine, detailLine);
    delay(KLcdResultMillis);
}


// =============================================================================
//  Serial helpers
// =============================================================================

// Translate the ESP32 security mode into a short readable name.
// "OPEN" means the network has no Wi-Fi password at all.
// Value	Mode	Meaning
// 0	OPEN	        No password and no encryption; anyone nearby can join and read the traffic.
// 1	WEP	            Old encryption from 1999, crackable in minutes. Treat it as insecure.
// 2	WPA_PSK	        WPA with a shared password (TKIP). Outdated and weak.
// 3	WPA2_PSK	    WPA2 with a shared password (AES). The common home standard.
// 4	WPA_WPA2_PSK	Mixed mode: accepts both WPA and WPA2 clients, for compatibility with old devices.
// 5	WPA2_ENTERPRISE	WPA2 where each user logs in with their own account through a RADIUS server (offices, eduroam).
// 6	WPA3_PSK	    WPA3 with a password (SAE). Resists offline password guessing. The current best for homes.
// 7	WPA2_WPA3_PSK	Transition mode: WPA3 for new devices, WPA2 for older ones.
// 8	WAPI_PSK	    Chinese national Wi-Fi standard with a password. Rare outside China.
// 9+	UNKNOWN	        Modes the code doesn’t map, e.g. 9 = OWE (Enhanced Open), 
// 10+  ENTERPRISE      WPA3-Enterprise variants.

const char* getAuthModeName(wifi_auth_mode_t authMode) {
    switch (authMode) {
        case WIFI_AUTH_OPEN:            return "OPEN";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA_PSK";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2_PSK";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA_WPA2_PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2_ENTERPRISE";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3_PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2_WPA3_PSK";
        case WIFI_AUTH_WAPI_PSK:        return "WAPI_PSK";
        default:                        return "UNKNOWN";
    }
}

// Print every byte of the SSID as two hex digits ("Hi" -> "4869").
// This is used in the Serial output so that spaces, symbols, and non-ASCII
// characters cannot break the line format.

void printSsidAsHex(const String& ssid) {
    for (size_t position = 0; position < ssid.length(); ++position) {
        const uint8_t character = static_cast<uint8_t>(ssid[position]);
        if (character < 0x10) {
            Serial.print('0');  // Keep two digits per byte
        }
        Serial.print(character, HEX);
    }
}

// Print the SSID as readable text in quotes, e.g. ssid="Summer-2G4".
// Hidden networks print as ssid="". Characters that are not printable
// ASCII, and the quote character itself, become '?' so the line format
// can never break.
// Example: ssid="My?Home?Network" for a network named "My\x01Home\x7FNetwork".

void printSsidAsText(const String& ssid) {
    Serial.print('"');
    for (size_t position = 0; position < ssid.length(); ++position) {
        const uint8_t character = static_cast<uint8_t>(ssid[position]);
        const bool isPrintable = character >= 32 && character <= 126;
        const bool isQuote = character == '"';
        Serial.print(isPrintable && !isQuote ? static_cast<char>(character) : '?');
    }
    Serial.print('"');
}

// Print one "AP ..." line for the network at 'index'.
// Example: "AP index=0 ssid_hex=4869 ssid=homewifi rssi=-67 channel=6 auth=WPA2_PSK"
// The SSID is printed as hex so that spaces, symbols, and non-ASCII
// characters can never break the line format.
// The SSID is also printed as readable text in quotes, e.g. ssid="Summer-2G4".
// Hidden networks print as ssid="". Characters that are not printable
// ASCII, and the quote character itself, become '?' so the line format
// can never break.

void printNetworkToSerial(int16_t index, const String& ssid) {
    Serial.print("AP index=");
    Serial.print(index);
    Serial.print(" ssid_hex=");
    printSsidAsHex(ssid);
    Serial.print(" ssid=");
    printSsidAsText(ssid);
    Serial.print(" rssi=");
    Serial.print(WiFi.RSSI(index));
    Serial.print(" channel=");
    Serial.print(WiFi.channel(index));
    Serial.print(" auth=");
    Serial.println(getAuthModeName(WiFi.encryptionType(index)));
}


// =============================================================================
//  Scan result handling
// =============================================================================

// Called when WiFi.scanNetworks() returns a negative error code.
// The error code is printed to the Serial monitor, and the LCD shows
// "Scan failed" for a few seconds.

void reportScanFailure(int16_t errorCode) {
    showOnLcd("Scan failed");

    Serial.print("SCAN_ERROR code=");
    Serial.println(errorCode);
    Serial.println("SCAN_END count=0 error=1");
}

// Called when the scan succeeded: show the count, then each network.
// The network count is printed to the Serial monitor, and each network
// is printed in a machine-readable format. Each network is also shown on the LCD
// for a few seconds, one at a time.
// The LCD shows the SSID on line 1, and the index, RSSI, and channel on line 2.
// Example LCD output for a network named "MyHomeNetwork":
//   line 1: "MyHomeNetwork   "
//   line 2: "3 -67dBm CH6"

void reportScanResults(int16_t networkCount) {
    char countLine[KLcdColumns + 1];
    snprintf(countLine, sizeof(countLine), "%d", networkCount);
    showOnLcd("Networks found:", countLine);
    delay(KLcdCountMillis);

    Serial.print("SCAN_RESULT count=");
    Serial.println(networkCount);

    for (int16_t index = 0; index < networkCount; ++index) {
        const String ssid = WiFi.SSID(index);
        showNetworkOnLcd(index, ssid);
        printNetworkToSerial(index, ssid);
    }

    Serial.print("SCAN_END count=");
    Serial.println(networkCount);
}


// =============================================================================
//  Main scan routine
// =============================================================================

// Run one complete scan and report it on the LCD and Serial.
// The scan is synchronous: this function does not return until the scan is done.
// The scan is asynchronous by default, but the ESP32 WiFi library provides a synchronous version that blocks until the scan is complete.
// The scan includes hidden networks (those that do not broadcast their SSID).
// The scan results are deleted from memory after reporting, to free up RAM.

void scanNetworks() {
    digitalWrite(KPinStatusLed, HIGH);  // LED on: scan in progress
    showOnLcd("Scanning WiFi...", "2.4 GHz");
    Serial.println("SCAN_BEGIN");

    // async = false : wait here until the scan is done
    // showHidden = true : include networks that hide their SSID
    const bool kAsync      = false;
    const bool kShowHidden = true;
    const int16_t networkCount = WiFi.scanNetworks(kAsync, kShowHidden);

    if (networkCount < 0) {
        reportScanFailure(networkCount);
    } else {
        reportScanResults(networkCount);
    }

    WiFi.scanDelete();                  // Free the memory used by the results
    digitalWrite(KPinStatusLed, LOW);   // LED off: scan finished
    lastScanMillis = millis();
    firstScan = false;
}


// =============================================================================
//  Arduino entry points
// =============================================================================
// The setup() function runs once when the ESP32-S3 is powered on or reset.
//  The loop() function runs repeatedly after setup() finishes.
// The loop() function checks if a scan is due, and if so, runs one scan.
// The loop() function also includes a small delay to avoid using 100% CPU.
// The scan is due if this is the first scan, or if the time since the last scan exceeds KScanIntervalMillis.
// The scan is synchronous, so the loop() function will block until the scan is complete.

void setup() {
    Serial.begin(115200);

    // Status LED starts off.
    pinMode(KPinStatusLed, OUTPUT);
    digitalWrite(KPinStatusLed, LOW);

    // Start the I2C bus on the existing LCD pins and show a welcome message.
    Wire.begin(KPinLcdSda, KPinLcdScl);
    lcd.begin(KLcdColumns, KLcdRows);
    lcd.setBacklight(255);
    showOnLcd("Lab 8 WiFi scan", "Starting...");

    // Station mode is required for scanning. Disconnect makes sure the
    // board is not joined to any network from a previous program.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    Serial.println("Lab 8 Wi-Fi scan ready.");
}

// tHe loop() function runs repeatedly after setup() finishes.
// The loop() function checks if a scan is due, and if so, runs one scan.

void loop() {
    // Unsigned subtraction keeps this correct when millis() wraps around.
    const bool scanIsDue = millis() - lastScanMillis >= KScanIntervalMillis;

    if (firstScan || scanIsDue) {
        scanNetworks();
    }
    delay(20);
}
