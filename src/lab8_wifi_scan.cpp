/*
 * Lab 8, step 1: scan nearby 2.4 GHz Wi-Fi networks.
 *
 * Existing hardware:
 *   LCD1602 / PCF8574T: SDA GPIO 1, SCL GPIO 2, address 0x27
 *   Yellow status LED: GPIO 13
 *
 * The ESP32-S3 scans 2.4 GHz channels. Full network details are written to
 * Serial; the LCD shows scan status and cycles through each network's SSID,
 * signal strength, and channel.
 */

#include <Arduino.h>
#include <LiquidCrystal_PCF8574.h>
#include <WiFi.h>
#include <Wire.h>

const uint8_t KPinLcdSda = 1;
const uint8_t KPinLcdScl = 2;
const uint8_t KPinStatusLed = 13;
const uint8_t KLcdAddress = 0x27;
const uint32_t KScanIntervalMillis = 15000;
const uint32_t KLcdResultMillis = 1200;

LiquidCrystal_PCF8574 lcd(KLcdAddress);
uint32_t lastScanMillis = 0;
bool firstScan = true;

const char* getAuthModeName(wifi_auth_mode_t authMode) {
    switch (authMode) {
        case WIFI_AUTH_OPEN:
            return "OPEN";
        case WIFI_AUTH_WEP:
            return "WEP";
        case WIFI_AUTH_WPA_PSK:
            return "WPA_PSK";
        case WIFI_AUTH_WPA2_PSK:
            return "WPA2_PSK";
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA_WPA2_PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return "WPA2_ENTERPRISE";
        case WIFI_AUTH_WPA3_PSK:
            return "WPA3_PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "WPA2_WPA3_PSK";
        case WIFI_AUTH_WAPI_PSK:
            return "WAPI_PSK";
        default:
            return "UNKNOWN";
    }
}

void printSsidHex(const String& ssid) {
    for (size_t index = 0; index < ssid.length(); ++index) {
        const uint8_t character = static_cast<uint8_t>(ssid[index]);
        if (character < 0x10) {
            Serial.print('0');
        }
        Serial.print(character, HEX);
    }
}

void displayAccessPoint(int16_t index, const String& ssid) {
    char displaySsid[17];
    for (size_t characterIndex = 0; characterIndex < sizeof(displaySsid) - 1;
         ++characterIndex) {
        displaySsid[characterIndex] = ' ';
    }
    displaySsid[sizeof(displaySsid) - 1] = '\0';

    const String label = ssid.isEmpty() ? String("(hidden SSID)") : ssid;
    const size_t displayLength =
        label.length() < sizeof(displaySsid) - 1 ? label.length() : sizeof(displaySsid) - 1;
    for (size_t characterIndex = 0; characterIndex < displayLength; ++characterIndex) {
        const uint8_t character = static_cast<uint8_t>(label[characterIndex]);
        displaySsid[characterIndex] =
            character >= 32 && character <= 126 ? character : '?';
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(displaySsid);
    lcd.setCursor(0, 1);
    lcd.print(index + 1);
    lcd.print(' ');
    lcd.print(WiFi.RSSI(index));
    lcd.print("dBm CH");
    lcd.print(WiFi.channel(index));
    delay(KLcdResultMillis);
}

void scanNetworks() {
    digitalWrite(KPinStatusLed, HIGH);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Scanning WiFi...");
    lcd.setCursor(0, 1);
    lcd.print("2.4 GHz");
    Serial.println("SCAN_BEGIN");

    const int16_t networkCount = WiFi.scanNetworks(false, true);

    if (networkCount < 0) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Scan failed");
        Serial.print("SCAN_ERROR code=");
        Serial.println(networkCount);
        Serial.println("SCAN_END count=0 error=1");
    } else {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Networks found:");
        lcd.setCursor(0, 1);
        lcd.print(networkCount);
        delay(1000);

        Serial.print("SCAN_RESULT count=");
        Serial.println(networkCount);

        for (int16_t index = 0; index < networkCount; ++index) {
            const String ssid = WiFi.SSID(index);
            displayAccessPoint(index, ssid);

            Serial.print("AP index=");
            Serial.print(index);
            Serial.print(" ssid_hex=");
            printSsidHex(ssid);
            Serial.print(" rssi=");
            Serial.print(WiFi.RSSI(index));
            Serial.print(" channel=");
            Serial.print(WiFi.channel(index));
            Serial.print(" auth=");
            Serial.println(getAuthModeName(WiFi.encryptionType(index)));
        }

        Serial.print("SCAN_END count=");
        Serial.println(networkCount);
    }

    WiFi.scanDelete();
    digitalWrite(KPinStatusLed, LOW);
    lastScanMillis = millis();
    firstScan = false;
}

void setup() {
    Serial.begin(115200);
    pinMode(KPinStatusLed, OUTPUT);
    digitalWrite(KPinStatusLed, LOW);

    Wire.begin(KPinLcdSda, KPinLcdScl);
    lcd.begin(16, 2);
    lcd.setBacklight(255);
    lcd.setCursor(0, 0);
    lcd.print("Lab 8 WiFi scan");
    lcd.setCursor(0, 1);
    lcd.print("Starting...");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    Serial.println("Lab 8 Wi-Fi scan ready.");
}

void loop() {
    if (firstScan || millis() - lastScanMillis >= KScanIntervalMillis) {
        scanNetworks();
    }
    delay(20);
}
