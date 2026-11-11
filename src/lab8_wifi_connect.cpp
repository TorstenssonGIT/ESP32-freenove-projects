/*
 * =============================================================================
 *  Lab 8 - Feature 3: Connect to Wi-Fi and check internet access (ESP32-S3)
 * =============================================================================
 *
 *  What it does
 *  ------------
 *  The ESP32-S3 joins the 2.4 GHz router network stored on the board (NVS)
 *  (Lab 8 test router: ZTE MC888 Pro), then every 30 seconds asks a
 *  connectivity-check page whether the internet can be reached:
 *
 *      http://connectivitycheck.gstatic.com/generate_204
 *
 *  That page answers "204 No Content" when the internet works. Plain HTTP
 *  is used on purpose: no certificates are needed on the ESP32.
 *
 *    - LCD    : connecting -> host name + IP -> "Internet OK" / "Internet FAIL".
 *    - Router : the ESP32 shows up as "lab8-esp32" in the client list.
 *    - Serial : one machine-readable block per check (used by the Python
 *               smoke-test lab8_wifi_connect_smoke_test.py).
 *    - LED    : on while connecting or checking.
 *
 *  If Wi-Fi drops, the next check reconnects automatically.
 *  Only the configured network is ever joined. The password is never
 *  printed on Serial or the LCD.
 *
 *  Hardware (unchanged from earlier labs)
 *  --------------------------------------
 *    LCD1602 + PCF8574T I2C adapter : SDA = GPIO 1, SCL = GPIO 2, addr 0x27
 *    Yellow status LED              : GPIO 13
 *
 *  Serial protocol (do not change - the smoke-test parses it)
 *  ---------------------------------------------------------
 *    CHECK_BEGIN
 *    WIFI_CONNECT_BEGIN ssid_hex=<hex> ssid="<text>"       (only when joining)
 *    WIFI_CONNECT_FAIL status=<n> reason=<NAME>            (only on failure)
 *    WIFI_STATUS connected=1 hostname=<name> ip=<ip> gateway=<ip>
 *                subnet=<mask> dns=<ip> rssi=<dBm> channel=<ch>
 *    HTTP_CHECK url=<url>
 *    HTTP_ERROR text="<reason>"                            (only if code < 0)
 *    HTTP_RESULT code=<http code> ok=<0|1> millis=<duration>
 *    CHECK_END ok=<0|1>
 *
 *  Credentials: typed in once over Serial and stored in NVS, never in the
 *  source or firmware. At boot, press 'c' in the serial monitor within 3 s
 *  to open the setup menu. See include/lab8_credentials.h.
 * =============================================================================
 */

#include <Arduino.h>
#include <HTTPClient.h>
#include <LiquidCrystal_PCF8574.h>
#include <WiFi.h>
#include <Wire.h>

// Credentials stored on the board (NVS) + Serial setup menu.
#include "lab8_credentials.h"

// -----------------------------------------------------------------------------
//  Pin and hardware settings
// -----------------------------------------------------------------------------
const uint8_t KPinLcdSda    = 1;     // I2C data line to the LCD adapter
const uint8_t KPinLcdScl    = 2;     // I2C clock line to the LCD adapter
const uint8_t KPinStatusLed = 13;    // Yellow LED, lit while working
const uint8_t KLcdAddress   = 0x27;  // PCF8574T default I2C address

const uint8_t KLcdColumns = 16;
const uint8_t KLcdRows    = 2;

// -----------------------------------------------------------------------------
//  Network identity
// -----------------------------------------------------------------------------
// Name the ESP32 reports to the router (DHCP). It shows up under this name
// in the MC888 Pro client list. Letters, digits and '-' only, max 32 chars.
const char* const KHostName = "lab8-esp32";

// -----------------------------------------------------------------------------
//  Internet check settings
// -----------------------------------------------------------------------------
const char* const KCheckUrl      = "http://connectivitycheck.gstatic.com/generate_204";
const int         KExpectedCode  = 204;   // "No Content" = internet works

// -----------------------------------------------------------------------------
//  Timing settings
// -----------------------------------------------------------------------------
const uint32_t KCheckIntervalMillis      = 30000;  // Time between checks
const uint32_t KWifiConnectTimeoutMillis = 20000;  // Give up joining after this
const uint32_t KWifiPollMillis           = 250;    // How often to poll status
const uint16_t KHttpTimeoutMillis        = 5000;   // Max wait for the page
const uint32_t KLcdStepMillis            = 1500;   // How long the IP shows

// -----------------------------------------------------------------------------
//  Global state
// -----------------------------------------------------------------------------
LiquidCrystal_PCF8574 lcd(KLcdAddress);

lab8::Credentials credentials;     // Loaded from NVS in setup()

uint32_t lastCheckMillis = 0;     // When the previous check finished
bool     firstCheck      = true;  // Check immediately after boot


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

// Copy text into a 16-character LCD line. Characters the LCD cannot show
// (outside printable ASCII) become '?'. 'output' needs KLcdColumns + 1 bytes.
void formatForLcd(const char* text, char* output) {
    uint8_t column = 0;
    for (; column < KLcdColumns && text[column] != '\0'; ++column) {
        const uint8_t character = static_cast<uint8_t>(text[column]);
        const bool isPrintable = character >= 32 && character <= 126;
        output[column] = isPrintable ? static_cast<char>(character) : '?';
    }
    output[column] = '\0';
}


// =============================================================================
//  Serial helpers
// =============================================================================

// Print every byte of the SSID as two hex digits ("Hi" -> "4869").
void printSsidAsHex(const char* ssid) {
    for (size_t position = 0; ssid[position] != '\0'; ++position) {
        const uint8_t character = static_cast<uint8_t>(ssid[position]);
        if (character < 0x10) {
            Serial.print('0');  // Keep two digits per byte
        }
        Serial.print(character, HEX);
    }
}

// Print the SSID as quoted text. Non-printable characters and '"' become '?'
// so the line format can never break.
void printSsidAsText(const char* ssid) {
    Serial.print('"');
    for (size_t position = 0; ssid[position] != '\0'; ++position) {
        const uint8_t character = static_cast<uint8_t>(ssid[position]);
        const bool isPrintable = character >= 32 && character <= 126;
        const bool isQuote = character == '"';
        Serial.print(isPrintable && !isQuote ? static_cast<char>(character) : '?');
    }
    Serial.print('"');
}

// Translate the Wi-Fi connection status into a short readable name.
//   NO_SSID_AVAIL  : network not found (wrong SSID, 5 GHz only, out of range)
//   CONNECT_FAILED : usually a wrong password
const char* getWifiStatusName(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS:     return "IDLE";
        case WL_NO_SSID_AVAIL:   return "NO_SSID_AVAIL";
        case WL_SCAN_COMPLETED:  return "SCAN_COMPLETED";
        case WL_CONNECTED:       return "CONNECTED";
        case WL_CONNECT_FAILED:  return "CONNECT_FAILED";
        case WL_CONNECTION_LOST: return "CONNECTION_LOST";
        case WL_DISCONNECTED:    return "DISCONNECTED";
        default:                 return "UNKNOWN";
    }
}


// =============================================================================
//  Wi-Fi connection
// =============================================================================

// Join the router network stored in NVS. Returns true when connected.
bool connectToWifi() {
    // Nothing stored yet: tell the user how to fix it.
    if (!credentials.hasRouter()) {
        Serial.println("WIFI_CONNECT_FAIL status=-1 reason=NO_CREDENTIALS");
        showOnLcd("No WiFi config", "Reset, press c");
        return false;
    }

    const char* ssid = credentials.routerSsid.c_str();

    char ssidLine[KLcdColumns + 1];
    formatForLcd(ssid, ssidLine);
    showOnLcd("Connecting WiFi", ssidLine);

    Serial.print("WIFI_CONNECT_BEGIN ssid_hex=");
    printSsidAsHex(ssid);
    Serial.print(" ssid=");
    printSsidAsText(ssid);
    Serial.println();

    WiFi.disconnect();                    // Start from a clean state
    WiFi.begin(ssid, credentials.routerPassword.c_str());

    // Wait until connected or the timeout runs out.
    const uint32_t startMillis = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - startMillis < KWifiConnectTimeoutMillis) {
        delay(KWifiPollMillis);
    }

    const wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
        return true;
    }

    // Failed: report why, so the user knows what to fix.
    Serial.print("WIFI_CONNECT_FAIL status=");
    Serial.print(static_cast<int>(status));
    Serial.print(" reason=");
    Serial.println(getWifiStatusName(status));

    char reasonLine[KLcdColumns + 1];
    formatForLcd(getWifiStatusName(status), reasonLine);
    showOnLcd("WiFi FAIL", reasonLine);
    return false;
}

// Make sure Wi-Fi is up; reconnect if it dropped. Returns true when connected.
bool ensureWifiConnected() {
    if (WiFi.status() == WL_CONNECTED) {
        return true;
    }
    return connectToWifi();
}

// Report the current connection on Serial and show name + IP on the LCD.
// All addresses below are handed out by the router (DHCP):
//   ip      : the ESP32's own address on the network
//   gateway : the router's address (the way out to the internet)
//   subnet  : which addresses count as "local", e.g. 255.255.255.0
//   dns     : server that turns names like "gstatic.com" into IP addresses
void reportWifiStatus() {
    const String ip = WiFi.localIP().toString();

    Serial.print("WIFI_STATUS connected=1 hostname=");
    Serial.print(WiFi.getHostname());
    Serial.print(" ip=");
    Serial.print(ip);
    Serial.print(" gateway=");
    Serial.print(WiFi.gatewayIP());
    Serial.print(" subnet=");
    Serial.print(WiFi.subnetMask());
    Serial.print(" dns=");
    Serial.print(WiFi.dnsIP());
    Serial.print(" rssi=");
    Serial.print(WiFi.RSSI());
    Serial.print(" channel=");
    Serial.println(WiFi.channel());

    char nameLine[KLcdColumns + 1];
    formatForLcd(WiFi.getHostname(), nameLine);
    showOnLcd(nameLine, ip.c_str());    // e.g. "lab8-esp32" / "192.168.0.123"
    delay(KLcdStepMillis);
}


// =============================================================================
//  Internet check
// =============================================================================

// Fetch the connectivity-check page. Returns true if it answered 204.
bool checkInternet() {
    showOnLcd("Checking web...", "generate_204");
    Serial.print("HTTP_CHECK url=");
    Serial.println(KCheckUrl);

    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(KHttpTimeoutMillis);
    http.setTimeout(KHttpTimeoutMillis);

    const uint32_t startMillis = millis();
    int code = -1;
    if (http.begin(client, KCheckUrl)) {
        code = http.GET();               // < 0 means no answer (DNS, timeout...)
    }
    const uint32_t durationMillis = millis() - startMillis;

    if (code < 0) {
        Serial.print("HTTP_ERROR text=\"");
        Serial.print(HTTPClient::errorToString(code));
        Serial.println('"');
    }
    http.end();

    const bool ok = code == KExpectedCode;
    Serial.print("HTTP_RESULT code=");
    Serial.print(code);
    Serial.print(" ok=");
    Serial.print(ok ? 1 : 0);
    Serial.print(" millis=");
    Serial.println(durationMillis);

    char detailLine[KLcdColumns + 1];
    snprintf(detailLine, sizeof(detailLine), "HTTP %d %lums",
             code, static_cast<unsigned long>(durationMillis));
    showOnLcd(ok ? "Internet OK" : "Internet FAIL", detailLine);
    return ok;
}


// =============================================================================
//  One complete check cycle
// =============================================================================

// Connect if needed, then check the internet. Result stays on the LCD
// until the next cycle.
void runCheckCycle() {
    digitalWrite(KPinStatusLed, HIGH);   // LED on: working
    Serial.println("CHECK_BEGIN");

    bool ok = false;
    if (ensureWifiConnected()) {
        reportWifiStatus();
        ok = checkInternet();
    }

    Serial.print("CHECK_END ok=");
    Serial.println(ok ? 1 : 0);

    digitalWrite(KPinStatusLed, LOW);    // LED off: idle
    lastCheckMillis = millis();
    firstCheck = false;
}


// =============================================================================
//  Arduino entry points
// =============================================================================

void setup() {
    Serial.begin(115200);

    // Status LED starts off.
    pinMode(KPinStatusLed, OUTPUT);
    digitalWrite(KPinStatusLed, LOW);

    // Start the I2C bus on the existing LCD pins and show a welcome message.
    Wire.begin(KPinLcdSda, KPinLcdScl);
    lcd.begin(KLcdColumns, KLcdRows);
    lcd.setBacklight(255);
    showOnLcd("Lab 8 WiFi web", "Starting...");

    // Load credentials from NVS. Open the setup menu if none are stored,
    // or if the user presses 'c' on Serial within 3 seconds.
    credentials = lab8::loadCredentials();
    lab8::printCredentialStatus(credentials);
    showOnLcd("WiFi setup:", "send c on Serial");
    if (!credentials.hasRouter() || lab8::setupRequested()) {
        showOnLcd("WiFi setup", "see Serial");
        lab8::runSetupMenu();
        credentials = lab8::loadCredentials();
        lab8::printCredentialStatus(credentials);
    }

    // The host name must be set BEFORE Wi-Fi starts, or the router
    // sees the default name ("esp32s3-XXXXXX").
    WiFi.setHostname(KHostName);

    // Station mode = the ESP32 joins a router as a normal client.
    WiFi.mode(WIFI_STA);

    Serial.println("Lab 8 Wi-Fi connect ready.");
}

void loop() {
    // Unsigned subtraction keeps this correct when millis() wraps around.
    const bool checkIsDue = millis() - lastCheckMillis >= KCheckIntervalMillis;

    if (firstCheck || checkIsDue) {
        runCheckCycle();
    }
    delay(20);
}
