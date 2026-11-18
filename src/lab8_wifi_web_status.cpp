/*
 * =============================================================================
 *  Lab 8 - Step 8.5: Web status page on the ESP32-S3
 * =============================================================================
 *
 *  What it does
 *  ------------
 *  Joins the router network stored on the board (NVS, see 8.4b), then:
 *    - checks internet access every 30 s (same as 8.4),
 *    - sets the clock from the internet (NTP, Swedish time incl. summer time),
 *    - scans nearby 2.4 GHz networks every 60 s without disconnecting,
 *    - measures connect time, uptime and number of reconnections,
 *    - serves a read-only status page on the LOCAL network only:
 *          http://<ip>/            HTML page, refreshes every 15 s
 *          http://lab8-esp32.local/  same page via mDNS (if the PC supports it)
 *          http://<ip>/api/status  the same data as JSON (smoke-test)
 *
 *  Security decisions (see lab8_wifi_security_analysis.md)
 *  -------------------------------------------------------
 *    - Read-only: only GET; anything else gets 405.
 *    - Only clients in the ESP32's own subnet are answered (403 otherwise).
 *    - Never shows passwords; JSON has no password fields at all.
 *    - SSIDs come from strangers' routers and may contain HTML/script.
 *      Every SSID is escaped for HTML and JSON before it is sent.
 *    - No JavaScript; Content-Security-Policy blocks scripts entirely.
 *    - Plain HTTP is accepted on the own WPA2 network; nothing is exposed
 *      to the internet (no port forwarding).
 *
 *  Hardware (unchanged)
 *  --------------------
 *    LCD1602 + PCF8574T I2C adapter : SDA = GPIO 1, SCL = GPIO 2, addr 0x27
 *    Yellow status LED              : GPIO 13 (on while connecting/checking)
 *
 *  Serial protocol
 *  ---------------
 *  The 8.4 block is unchanged, so the 8.4 smoke-test still passes:
 *    CHECK_BEGIN / WIFI_CONNECT_BEGIN / WIFI_STATUS / HTTP_CHECK /
 *    HTTP_RESULT / CHECK_END
 *  New lines:
 *    WIFI_CONNECTED millis=<ms> attempt=<n>         (after each connect)
 *    WIFI_STATS uptime_s=<s> connect_ms=<ms> connect_attempts=<n> reconnects=<n>
 *    WEB_READY url=http://<ip>/ mdns=http://lab8-esp32.local/
 *    SCAN_DONE count=<n>
 *    NTP_SYNC ok=1 epoch=<unix time> local="<YYYY-MM-DD HH:MM:SS TZ>"  (once)
 * =============================================================================
 */

#include <Arduino.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <LiquidCrystal_PCF8574.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_timer.h>
#include <time.h>

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
//  Network identity and web server
// -----------------------------------------------------------------------------
const char* const KHostName    = "lab8-esp32";  // DHCP name and mDNS name
const uint16_t    KWebPort     = 80;
const uint8_t     KPageRefreshSeconds = 15;     // HTML page reloads itself

// -----------------------------------------------------------------------------
//  Clock (NTP)
// -----------------------------------------------------------------------------
// Europe/Stockholm as a POSIX rule: CET (UTC+1), summer time CEST (UTC+2)
// from the last Sunday in March 02:00 to the last Sunday in October 03:00.
const char* const KTimeZone   = "CET-1CEST,M3.5.0,M10.5.0/3";
const char* const KNtpServer1 = "se.pool.ntp.org";
const char* const KNtpServer2 = "pool.ntp.org";
// Any time before this (2025-01-01) means the clock has not been set yet.
const time_t      KValidTimeAfter = 1735689600;

// -----------------------------------------------------------------------------
//  Internet check settings (same as 8.4)
// -----------------------------------------------------------------------------
const char* const KCheckUrl     = "http://connectivitycheck.gstatic.com/generate_204";
const int         KExpectedCode = 204;

// -----------------------------------------------------------------------------
//  Timing settings
// -----------------------------------------------------------------------------
const uint32_t KCheckIntervalMillis      = 30000;  // Internet check
const uint32_t KScanIntervalMillis       = 60000;  // Background scan
const uint32_t KWifiConnectTimeoutMillis = 20000;  // Give up joining after this
const uint32_t KWifiPollMillis           = 250;    // Status poll while joining
const uint16_t KHttpTimeoutMillis        = 5000;   // Max wait for the check page
const uint32_t KLcdStepMillis            = 1500;   // How long name + IP shows

// -----------------------------------------------------------------------------
//  Scan result storage
// -----------------------------------------------------------------------------
const uint8_t KMaxStoredNetworks = 40;   // Keep memory use bounded

struct NetworkInfo {
    String           ssid;
    int32_t          rssi;
    int32_t          channel;
    wifi_auth_mode_t auth;
};

// -----------------------------------------------------------------------------
//  Global state
// -----------------------------------------------------------------------------
LiquidCrystal_PCF8574 lcd(KLcdAddress);
WebServer             server(KWebPort);
lab8::Credentials     credentials;           // Loaded from NVS in setup()

// Connection statistics
uint32_t connectAttempts  = 0;     // WiFi.begin() calls since boot
uint32_t connectSuccesses = 0;     // Successful connections since boot
uint32_t lastConnectMillis = 0;    // Duration of the latest successful connect
bool     webStarted       = false; // Web server + mDNS running

// Clock
bool     ntpStarted       = false; // configTzTime() called
bool     ntpSyncReported  = false; // NTP_SYNC line printed

// Latest internet check
int      lastHttpCode       = 0;
bool     lastHttpOk         = false;
uint32_t lastHttpMillis     = 0;   // Duration of the request
uint32_t lastCheckMillis    = 0;   // When the previous check finished
bool     firstCheck         = true;

// Latest scan
NetworkInfo storedNetworks[KMaxStoredNetworks];
uint8_t     storedNetworkCount = 0;
int16_t     totalNetworksFound = -1;   // -1 = no scan finished yet
uint32_t    lastScanMillis     = 0;    // When the latest scan finished
uint32_t    scanStartMillis    = 0;
bool        scanRunning        = false;


// =============================================================================
//  Small helpers
// =============================================================================

// Seconds since boot. esp_timer is 64-bit, so it does not wrap like millis().
// True once NTP has set the clock.
bool clockIsSet() {
    return time(nullptr) > KValidTimeAfter;
}

// Local time as text, e.g. "2026-11-17 15:40:12 CET"; empty if not set.
String localTimeText() {
    if (!clockIsSet()) {
        return String();
    }
    const time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", &local);
    return String(buffer);
}

// Start NTP once Wi-Fi is up. The ESP32 then keeps the clock running and
// re-syncs in the background (about once an hour).
void startClock() {
    if (ntpStarted) {
        return;
    }
    configTzTime(KTimeZone, KNtpServer1, KNtpServer2);
    ntpStarted = true;
}

// Print NTP_SYNC once, when the clock has been set for the first time.
void reportClockIfNewlySet() {
    if (ntpSyncReported || !clockIsSet()) {
        return;
    }
    ntpSyncReported = true;
    Serial.print("NTP_SYNC ok=1 epoch=");
    Serial.print(static_cast<uint32_t>(time(nullptr)));
    Serial.print(" local=\"");
    Serial.print(localTimeText());
    Serial.println('"');
}

uint32_t uptimeSeconds() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
}

// Seconds since a millis() timestamp (0 if it never happened).
uint32_t ageSeconds(uint32_t timestampMillis) {
    return timestampMillis == 0 ? 0 : (millis() - timestampMillis) / 1000;
}

// Clear the LCD and write up to two lines of text.
void showOnLcd(const char* line1, const char* line2 = "") {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(line1);
    lcd.setCursor(0, 1);
    lcd.print(line2);
}

// Copy text into a 16-character LCD line; non-printable characters become '?'.
void formatForLcd(const char* text, char* output) {
    uint8_t column = 0;
    for (; column < KLcdColumns && text[column] != '\0'; ++column) {
        const uint8_t character = static_cast<uint8_t>(text[column]);
        const bool isPrintable = character >= 32 && character <= 126;
        output[column] = isPrintable ? static_cast<char>(character) : '?';
    }
    output[column] = '\0';
}

// Readable name of a Wi-Fi security mode (same names as the 8.1 scan).
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

// Readable name of the connection status (same as 8.4).
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
//  Escaping - SSIDs are chosen by strangers and must never be trusted
// =============================================================================

// SSID as two hex digits per byte ("Hi" -> "4869"), exact and always safe.
String ssidToHex(const String& ssid) {
    static const char KHexDigits[] = "0123456789ABCDEF";
    String hex;
    for (size_t index = 0; index < ssid.length(); ++index) {
        const uint8_t byte = static_cast<uint8_t>(ssid[index]);
        hex += KHexDigits[byte >> 4];
        hex += KHexDigits[byte & 0x0F];
    }
    return hex;
}

// Keep printable ASCII only; everything else becomes '?' (as on Serial).
String toPrintableAscii(const String& text) {
    String result;
    for (size_t index = 0; index < text.length(); ++index) {
        const uint8_t character = static_cast<uint8_t>(text[index]);
        result += (character >= 32 && character <= 126) ? static_cast<char>(character) : '?';
    }
    return result;
}

// Escape text for HTML so an SSID like "<script>" is shown, never run.
String escapeHtml(const String& text) {
    String result;
    for (size_t index = 0; index < text.length(); ++index) {
        const char character = text[index];
        switch (character) {
            case '&':  result += "&amp;";  break;
            case '<':  result += "&lt;";   break;
            case '>':  result += "&gt;";   break;
            case '"':  result += "&quot;"; break;
            case '\'': result += "&#39;";  break;
            default:   result += character; break;
        }
    }
    return result;
}

// Escape printable-ASCII text for a JSON string.
String escapeJson(const String& text) {
    String result;
    for (size_t index = 0; index < text.length(); ++index) {
        const char character = text[index];
        if (character == '"' || character == '\\') {
            result += '\\';
        }
        result += character;
    }
    return result;
}

// Safe display form of an SSID for HTML and JSON.
String safeSsid(const String& ssid) {
    return ssid.isEmpty() ? String("(hidden SSID)") : toPrintableAscii(ssid);
}


// =============================================================================
//  Wi-Fi connection (8.4 + timing)
// =============================================================================

void printSsidFields(const String& ssid) {
    Serial.print("ssid_hex=");
    Serial.print(ssidToHex(ssid));
    Serial.print(" ssid=\"");
    String text = toPrintableAscii(ssid);
    text.replace('"', '?');              // Keep the quoted field intact
    Serial.print(text);
    Serial.print('"');
}

// Join the router network stored in NVS. Returns true when connected.
bool connectToWifi() {
    if (!credentials.hasRouter()) {
        Serial.println("WIFI_CONNECT_FAIL status=-1 reason=NO_CREDENTIALS");
        showOnLcd("No WiFi config", "Reset, press c");
        return false;
    }

    char ssidLine[KLcdColumns + 1];
    formatForLcd(credentials.routerSsid.c_str(), ssidLine);
    showOnLcd("Connecting WiFi", ssidLine);

    Serial.print("WIFI_CONNECT_BEGIN ");
    printSsidFields(credentials.routerSsid);
    Serial.println();

    WiFi.disconnect();
    ++connectAttempts;
    const uint32_t startMillis = millis();
    WiFi.begin(credentials.routerSsid.c_str(), credentials.routerPassword.c_str());

    while (WiFi.status() != WL_CONNECTED &&
           millis() - startMillis < KWifiConnectTimeoutMillis) {
        delay(KWifiPollMillis);
    }

    const wl_status_t status = WiFi.status();
    if (status != WL_CONNECTED) {
        Serial.print("WIFI_CONNECT_FAIL status=");
        Serial.print(static_cast<int>(status));
        Serial.print(" reason=");
        Serial.println(getWifiStatusName(status));

        char reasonLine[KLcdColumns + 1];
        formatForLcd(getWifiStatusName(status), reasonLine);
        showOnLcd("WiFi FAIL", reasonLine);
        return false;
    }

    // Connected: record how long it took.
    lastConnectMillis = millis() - startMillis;
    ++connectSuccesses;
    Serial.print("WIFI_CONNECTED millis=");
    Serial.print(lastConnectMillis);
    Serial.print(" attempt=");
    Serial.println(connectAttempts);
    return true;
}

bool ensureWifiConnected() {
    return WiFi.status() == WL_CONNECTED || connectToWifi();
}

// Number of reconnections = successful connects after the first one.
uint32_t reconnectCount() {
    return connectSuccesses > 0 ? connectSuccesses - 1 : 0;
}

// Same WIFI_STATUS line as 8.4 (the 8.4 smoke-test parses it), plus stats.
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

    Serial.print("WIFI_STATS uptime_s=");
    Serial.print(uptimeSeconds());
    Serial.print(" connect_ms=");
    Serial.print(lastConnectMillis);
    Serial.print(" connect_attempts=");
    Serial.print(connectAttempts);
    Serial.print(" reconnects=");
    Serial.println(reconnectCount());
}


// =============================================================================
//  Internet check (same as 8.4)
// =============================================================================

bool checkInternet() {
    Serial.print("HTTP_CHECK url=");
    Serial.println(KCheckUrl);

    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(KHttpTimeoutMillis);
    http.setTimeout(KHttpTimeoutMillis);

    const uint32_t startMillis = millis();
    int code = -1;
    if (http.begin(client, KCheckUrl)) {
        code = http.GET();
    }
    lastHttpMillis = millis() - startMillis;

    if (code < 0) {
        Serial.print("HTTP_ERROR text=\"");
        Serial.print(HTTPClient::errorToString(code));
        Serial.println('"');
    }
    http.end();

    lastHttpCode = code;
    lastHttpOk   = code == KExpectedCode;
    Serial.print("HTTP_RESULT code=");
    Serial.print(code);
    Serial.print(" ok=");
    Serial.print(lastHttpOk ? 1 : 0);
    Serial.print(" millis=");
    Serial.println(lastHttpMillis);
    return lastHttpOk;
}


// =============================================================================
//  Background scan (does not disconnect, does not block the web server)
// =============================================================================

// Start a scan in the background. WiFi.scanComplete() reports when it is done.
void startScan() {
    const bool kAsync      = true;
    const bool kShowHidden = true;
    WiFi.scanNetworks(kAsync, kShowHidden);
    scanRunning     = true;
    scanStartMillis = millis();
}

// Copy finished results into our own storage, then free the driver's copy.
void collectScanIfDone() {
    if (!scanRunning) {
        return;
    }
    const int16_t result = WiFi.scanComplete();
    if (result == WIFI_SCAN_RUNNING) {
        return;                            // Still scanning
    }

    scanRunning = false;
    if (result < 0) {                      // Failed: try again next interval
        Serial.println("SCAN_DONE count=-1 error=1");
        WiFi.scanDelete();
        lastScanMillis = millis();
        return;
    }

    totalNetworksFound = result;
    storedNetworkCount = result < KMaxStoredNetworks ? result : KMaxStoredNetworks;
    for (uint8_t index = 0; index < storedNetworkCount; ++index) {
        storedNetworks[index].ssid    = WiFi.SSID(index);
        storedNetworks[index].rssi    = WiFi.RSSI(index);
        storedNetworks[index].channel = WiFi.channel(index);
        storedNetworks[index].auth    = WiFi.encryptionType(index);
    }
    WiFi.scanDelete();
    lastScanMillis = millis();

    Serial.print("SCAN_DONE count=");
    Serial.println(totalNetworksFound);
}


// =============================================================================
//  Web server
// =============================================================================

// Security headers sent with every response.
void sendSecurityHeaders() {
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("X-Content-Type-Options", "nosniff");
    server.sendHeader("X-Frame-Options", "DENY");
    server.sendHeader("Referrer-Policy", "no-referrer");
    // No scripts at all; only the page's own inline styles.
    server.sendHeader("Content-Security-Policy",
                      "default-src 'none'; style-src 'unsafe-inline'");
}

// Answer only clients in the ESP32's own subnet (local network only).
bool clientIsLocal() {
    const uint32_t mask   = static_cast<uint32_t>(WiFi.subnetMask());
    const uint32_t local  = static_cast<uint32_t>(WiFi.localIP());
    const uint32_t remote = static_cast<uint32_t>(server.client().remoteIP());
    return (remote & mask) == (local & mask);
}

// Common checks for every request. Returns false if already answered.
bool acceptRequest() {
    if (!clientIsLocal()) {
        sendSecurityHeaders();
        server.send(403, "text/plain", "Forbidden: local network only\n");
        return false;
    }
    return true;
}

// Build the JSON status. Contains NO password fields.
String buildStatusJson() {
    String json;
    json.reserve(4096);

    json += "{\"device\":{";
    json += "\"hostname\":\"" + escapeJson(WiFi.getHostname()) + "\",";
    json += "\"uptime_s\":" + String(uptimeSeconds()) + ",";
    json += "\"step\":\"8.5\",";
    json += "\"ntp_synced\":" + String(clockIsSet() ? "true" : "false") + ",";
    json += "\"epoch\":" + String(clockIsSet() ? static_cast<uint32_t>(time(nullptr)) : 0u) + ",";
    json += "\"local_time\":\"" + localTimeText() + "\"},";

    const bool connected = WiFi.status() == WL_CONNECTED;
    json += "\"wifi\":{";
    json += "\"connected\":" + String(connected ? "true" : "false") + ",";
    json += "\"ssid\":\"" + escapeJson(safeSsid(credentials.routerSsid)) + "\",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"gateway\":\"" + WiFi.gatewayIP().toString() + "\",";
    json += "\"subnet\":\"" + WiFi.subnetMask().toString() + "\",";
    json += "\"dns\":\"" + WiFi.dnsIP().toString() + "\",";
    json += "\"mac\":\"" + WiFi.macAddress() + "\",";
    json += "\"rssi\":" + String(connected ? WiFi.RSSI() : 0) + ",";
    json += "\"channel\":" + String(connected ? WiFi.channel() : 0) + ",";
    json += "\"connect_ms\":" + String(lastConnectMillis) + ",";
    json += "\"connect_attempts\":" + String(connectAttempts) + ",";
    json += "\"reconnects\":" + String(reconnectCount()) + "},";

    json += "\"internet\":{";
    json += "\"url\":\"" + String(KCheckUrl) + "\",";
    json += "\"ok\":" + String(lastHttpOk ? "true" : "false") + ",";
    json += "\"http_code\":" + String(lastHttpCode) + ",";
    json += "\"millis\":" + String(lastHttpMillis) + ",";
    json += "\"age_s\":" + String(ageSeconds(lastCheckMillis)) + "},";

    json += "\"scan\":{";
    json += "\"count\":" + String(totalNetworksFound) + ",";
    json += "\"stored\":" + String(storedNetworkCount) + ",";
    json += "\"age_s\":" + String(ageSeconds(lastScanMillis)) + ",";
    json += "\"networks\":[";
    for (uint8_t index = 0; index < storedNetworkCount; ++index) {
        const NetworkInfo& network = storedNetworks[index];
        if (index > 0) {
            json += ',';
        }
        json += "{\"ssid\":\"" + escapeJson(safeSsid(network.ssid)) + "\",";
        json += "\"ssid_hex\":\"" + ssidToHex(network.ssid) + "\",";
        json += "\"rssi\":" + String(network.rssi) + ",";
        json += "\"channel\":" + String(network.channel) + ",";
        json += "\"auth\":\"" + String(getAuthModeName(network.auth)) + "\"}";
    }
    json += "]}}";
    return json;
}

// One "label: value" row in the HTML tables.
String htmlRow(const String& label, const String& value) {
    return "<tr><th>" + escapeHtml(label) + "</th><td>" + escapeHtml(value) + "</td></tr>";
}

// Build the HTML page. Every value from outside is escaped.
String buildStatusHtml() {
    const bool connected = WiFi.status() == WL_CONNECTED;

    String html;
    html.reserve(6144);
    html += "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">";
    html += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
    html += "<meta http-equiv=\"refresh\" content=\"" + String(KPageRefreshSeconds) + "\">";
    html += "<title>lab8-esp32 status</title><style>";
    html += "body{font-family:system-ui,sans-serif;margin:16px;color:#1d2430;background:#fff}";
    html += "h1{font-size:1.3em}h2{font-size:1.05em;margin-top:1.4em}";
    html += "table{border-collapse:collapse;width:100%;max-width:720px}";
    html += "th,td{border:1px solid #c3ccd7;padding:4px 6px;text-align:left;font-size:.9em}";
    html += "th{background:#e8eef5}.ok{color:#17803d;font-weight:bold}";
    html += ".bad{color:#b42318;font-weight:bold}.open{background:#fff4e5}";
    html += "small{color:#667085}</style></head><body>";

    html += "<h1>Lab 8 &ndash; " + escapeHtml(WiFi.getHostname()) + "</h1>";
    html += "<small>Read-only status page, local network only. Refreshes every ";
    html += String(KPageRefreshSeconds) + " s.</small>";

    // Connection
    html += "<h2>Connection</h2><table>";
    html += htmlRow("Wi-Fi", connected ? "Connected" : "Not connected");
    html += htmlRow("Network (SSID)", safeSsid(credentials.routerSsid));
    html += htmlRow("IP address", WiFi.localIP().toString());
    html += htmlRow("Gateway", WiFi.gatewayIP().toString());
    html += htmlRow("Subnet mask", WiFi.subnetMask().toString());
    html += htmlRow("DNS server", WiFi.dnsIP().toString());
    html += htmlRow("MAC address", WiFi.macAddress());
    html += htmlRow("Signal (RSSI)", String(connected ? WiFi.RSSI() : 0) + " dBm");
    html += htmlRow("Channel", String(connected ? WiFi.channel() : 0));
    html += htmlRow("Last connect time", String(lastConnectMillis) + " ms");
    html += htmlRow("Connect attempts / reconnects",
                    String(connectAttempts) + " / " + String(reconnectCount()));
    html += htmlRow("Uptime", String(uptimeSeconds()) + " s");
    html += htmlRow("Local time (NTP)", clockIsSet() ? localTimeText() : String("not set yet"));
    html += "</table>";

    // Internet
    html += "<h2>Internet</h2><table>";
    html += "<tr><th>Status</th><td class=\"" + String(lastHttpOk ? "ok" : "bad") + "\">";
    html += lastHttpOk ? "Internet OK" : "Internet FAIL";
    html += "</td></tr>";
    html += htmlRow("Check URL", KCheckUrl);
    html += htmlRow("HTTP code", String(lastHttpCode));
    html += htmlRow("Response time", String(lastHttpMillis) + " ms");
    html += htmlRow("Checked", String(ageSeconds(lastCheckMillis)) + " s ago");
    html += "</table>";

    // Scan
    html += "<h2>Nearby networks</h2>";
    if (totalNetworksFound < 0) {
        html += "<p>First scan not finished yet.</p>";
    } else {
        html += "<p>" + String(totalNetworksFound) + " networks, scanned ";
        html += String(ageSeconds(lastScanMillis)) + " s ago. Open networks are highlighted.</p>";
        html += "<table><tr><th>#</th><th>SSID</th><th>RSSI</th><th>Ch</th><th>Security</th></tr>";
        for (uint8_t index = 0; index < storedNetworkCount; ++index) {
            const NetworkInfo& network = storedNetworks[index];
            const bool isOpen = network.auth == WIFI_AUTH_OPEN;
            html += isOpen ? "<tr class=\"open\">" : "<tr>";
            html += "<td>" + String(index + 1) + "</td>";
            html += "<td>" + escapeHtml(safeSsid(network.ssid)) + "</td>";
            html += "<td>" + String(network.rssi) + "</td>";
            html += "<td>" + String(network.channel) + "</td>";
            html += "<td>" + String(getAuthModeName(network.auth)) + "</td></tr>";
        }
        html += "</table>";
    }

    html += "<p><small>JSON: <a href=\"/api/status\">/api/status</a></small></p>";
    html += "</body></html>";
    return html;
}

void handleRoot() {
    if (!acceptRequest()) {
        return;
    }
    sendSecurityHeaders();
    server.send(200, "text/html; charset=utf-8", buildStatusHtml());
}

void handleApiStatus() {
    if (!acceptRequest()) {
        return;
    }
    sendSecurityHeaders();
    server.send(200, "application/json", buildStatusJson());
}

// Unknown path -> 404. Any method other than GET -> 405 (read-only).
void handleNotFound() {
    if (!acceptRequest()) {
        return;
    }
    sendSecurityHeaders();
    if (server.method() != HTTP_GET) {
        server.sendHeader("Allow", "GET");
        server.send(405, "text/plain", "Method not allowed: read-only\n");
        return;
    }
    server.send(404, "text/plain", "Not found\n");
}

// Start the web server and mDNS once Wi-Fi is up.
void startWebServer() {
    startClock();                         // NTP needs Wi-Fi, so start it here
    if (webStarted) {
        return;
    }
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/status", HTTP_GET, handleApiStatus);
    server.onNotFound(handleNotFound);
    server.begin();

    if (MDNS.begin(KHostName)) {
        MDNS.addService("http", "tcp", KWebPort);
    } else {
        Serial.println("MDNS_ERROR");
    }
    webStarted = true;

    Serial.print("WEB_READY url=http://");
    Serial.print(WiFi.localIP());
    Serial.print("/ mdns=http://");
    Serial.print(KHostName);
    Serial.println(".local/");
}


// =============================================================================
//  One complete check cycle (8.4 protocol)
// =============================================================================

void runCheckCycle() {
    digitalWrite(KPinStatusLed, HIGH);
    Serial.println("CHECK_BEGIN");

    bool ok = false;
    if (ensureWifiConnected()) {
        reportWifiStatus();
        startWebServer();
        ok = checkInternet();
    }

    Serial.print("CHECK_END ok=");
    Serial.println(ok ? 1 : 0);

    // LCD: internet status on line 1, the address to browse to on line 2.
    if (WiFi.status() == WL_CONNECTED) {
        const String ip = WiFi.localIP().toString();
        showOnLcd(ok ? "Internet OK" : "Internet FAIL", ip.c_str());
    }

    digitalWrite(KPinStatusLed, LOW);
    lastCheckMillis = millis();
    firstCheck = false;
}


// =============================================================================
//  Arduino entry points
// =============================================================================

void setup() {
    Serial.begin(115200);

    pinMode(KPinStatusLed, OUTPUT);
    digitalWrite(KPinStatusLed, LOW);

    Wire.begin(KPinLcdSda, KPinLcdScl);
    lcd.begin(KLcdColumns, KLcdRows);
    lcd.setBacklight(255);
    showOnLcd("Lab 8 WiFi web", "Starting...");

    // Credentials from NVS; setup menu if none stored or 'c' pressed.
    credentials = lab8::loadCredentials();
    lab8::printCredentialStatus(credentials);
    showOnLcd("WiFi setup:", "send c on Serial");
    if (!credentials.hasRouter() || lab8::setupRequested()) {
        showOnLcd("WiFi setup", "see Serial");
        lab8::runSetupMenu();
        credentials = lab8::loadCredentials();
        lab8::printCredentialStatus(credentials);
    }

    // Host name BEFORE Wi-Fi starts, then station mode.
    WiFi.setHostname(KHostName);
    WiFi.mode(WIFI_STA);

    Serial.println("Lab 8 Wi-Fi web status ready.");
}

void loop() {
    // Serve web requests as often as possible.
    if (webStarted) {
        server.handleClient();
    }

    // Internet check (and reconnect if needed) every 30 s.
    if (firstCheck || millis() - lastCheckMillis >= KCheckIntervalMillis) {
        runCheckCycle();
    }

    // Background scan every 60 s while connected; first one right away.
    const bool scanDue = lastScanMillis == 0 ||
                         millis() - lastScanMillis >= KScanIntervalMillis;
    if (!scanRunning && scanDue && WiFi.status() == WL_CONNECTED) {
        startScan();
    }
    collectScanIfDone();
    reportClockIfNewlySet();

    delay(2);
}
