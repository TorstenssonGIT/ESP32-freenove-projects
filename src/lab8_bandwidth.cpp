/*
 * =============================================================================
 *  Lab 8 - 8.6 Bandwidth test: one firmware, role chosen by a jumper
 * =============================================================================
 *
 *  Role jumper
 *  -----------
 *    GPIO 42 to GND  -> ECHO SERVER  (board B, host name lab8-echo)
 *    GPIO 42 open    -> CONTROLLER   (board A, host name lab8-esp32, LCD)
 *  The same build is uploaded to both boards.
 *
 *  Both roles
 *  ----------
 *    - Join the router (credentials from NVS; 'c' within 3 s = setup menu).
 *    - Wi-Fi power saving off (NFR-RT-2), mDNS name <host name>.local.
 *    - INA226 on Wire, GPIO 1/2, found by ID (HW-3).
 *
 *  Echo server
 *  -----------
 *    UDP port 5001: "STATUS?" -> STATUS line (uptime + own power), anything
 *    else -> sent back unchanged. Only the local subnet, max 1472 bytes (SEC-7).
 *    POWER line every 5 s.
 *
 *  Controller
 *  ----------
 *    Finds the echo server (mDNS lab8-echo.local, else KEchoFallbackIp), runs
 *    one series at boot and again on 'r':
 *      - echo server STATUS before and after (restart check, its power)
 *      - sizes 32...1472 bytes, 100 packets each, one at a time, 500 ms
 *        timeout, sequence number + content checked
 *      - min / median / average / max RTT (µs, esp_timer), losses,
 *        throughput, own power; each size shown on the LCD
 *    Throughput = 2 x size x 8 / average RTT  (bits both ways per round trip)
 *
 *  Requirements: FR-BW-1...4, FR-BW-6, FR-BW-7, NFR-RT-2, NFR-RT-3, SEC-7.
 *  Not yet: direct mode (FR-BW-5), web page output (FR-BW-4).
 *
 *  Serial protocol
 *  ---------------
 *    ROLE role=<echo|controller>
 *    INA226 ok=<0|1> addr=<0x4?>
 *    WIFI_STATUS connected=1 hostname=<name> ip=<ip> rssi=<dBm> channel=<n>
 *    echo server:
 *      ECHO_READY port=5001
 *      POWER uptime_s=<s> v=<V> i_ma=<mA> p_mw=<mW> echoed=<n> rejected=<n>
 *    controller:
 *      ECHO_TARGET ip=<ip> via=<mdns|fallback>
 *      ECHO_STATUS when=<before|after> uptime_ms=<ms> v=<V> i_ma=<mA> p_mw=<mW>
 *      BW_RESULT size=<B> sent=<n> lost=<n> rtt_min_us=.. rtt_med_us=..
 *                rtt_avg_us=.. rtt_max_us=.. kbps=.. a_v=.. a_i_ma=.. a_p_mw=..
 *      BW_SERIES done=1 echo_restarted=<0|1> rssi=<dBm>
 *
 *  platformio.ini:  build_src_filter = -<*> +<lab8_bandwidth.cpp>
 * =============================================================================
 */

#include <Arduino.h>
#include <ESPmDNS.h>
#include <LiquidCrystal_PCF8574.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <algorithm>
#include <esp_timer.h>

#include "lab8_credentials.h"

// ---------------------------------------------------------------------------
//  Settings
// ---------------------------------------------------------------------------
constexpr uint8_t     KPinRole = 42;              // jumper to GND = echo server

constexpr const char* KControllerName = "lab8-esp32";
constexpr const char* KEchoName       = "lab8-echo";
const IPAddress       KEchoFallbackIp(192, 168, 32, 51);
constexpr uint16_t    KEchoPort  = 5001;
constexpr uint16_t    KLocalPort = 5002;
constexpr size_t      KMaxPacketBytes = 1472;    // largest UDP payload without fragmentation

constexpr uint16_t KSizes[]        = {32, 64, 128, 256, 512, 1024, 1472};
constexpr int      KPacketsPerSize = 100;
constexpr uint32_t KTimeoutUs      = 500000;     // 500 ms = lost
constexpr int      KWarmupPackets  = 5;          // fills ARP tables, not counted

constexpr uint32_t KWifiTimeoutMs   = 20000;
constexpr uint32_t KPowerIntervalMs = 5000;

constexpr uint8_t  KBusSda = 1, KBusScl = 2;     // LCD (A) + INA226 (A and B)
constexpr uint32_t KBusHz  = 100000;
constexpr uint8_t  KLcdAddress = 0x27;

constexpr float   KShuntOhm      = 0.100f;       // R100
constexpr uint8_t KInaRegShunt   = 0x01;         // 2.5 µV/LSB
constexpr uint8_t KInaRegBus     = 0x02;         // 1.25 mV/LSB
constexpr uint8_t KInaRegManufId = 0xFE;         // 0x5449 = "TI"

// ---------------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------------
bool isEchoServer = false;

lab8::Credentials     credentials;
LiquidCrystal_PCF8574 lcd(KLcdAddress);
WiFiUDP   udp;
IPAddress echoIp;
uint8_t   txBuffer[KMaxPacketBytes];
uint8_t   rxBuffer[1500];
uint32_t  rtts[KPacketsPerSize];
uint8_t   inaAddress = 0;

uint32_t echoedPackets   = 0;
uint32_t rejectedPackets = 0;
uint32_t lastPowerMillis = 0;

struct Power { float volts = 0, milliamps = 0, milliwatts = 0; };
struct EchoStatus { bool ok = false; uint32_t uptimeMs = 0; Power power; };

// ---------------------------------------------------------------------------
//  LCD (controller only) and INA226
// ---------------------------------------------------------------------------
void showOnLcd(const char* line1, const char* line2) {
    if (isEchoServer) return;                     // board B has no LCD
    char padded[17];
    lcd.setCursor(0, 0); snprintf(padded, sizeof(padded), "%-16s", line1); lcd.print(padded);
    lcd.setCursor(0, 1); snprintf(padded, sizeof(padded), "%-16s", line2); lcd.print(padded);
}

uint16_t inaRead(uint8_t reg) {
    Wire.beginTransmission(inaAddress);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return 0;
    if (Wire.requestFrom(inaAddress, (uint8_t)2) != 2) return 0;
    uint16_t high = Wire.read();
    return (high << 8) | Wire.read();
}

bool setupIna() {
    for (uint8_t address = 0x40; address <= 0x4F; address++) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() != 0) continue;
        inaAddress = address;
        if (inaRead(KInaRegManufId) == 0x5449) {
            Serial.printf("INA226 ok=1 addr=0x%02X\n", address);
            return true;
        }
    }
    inaAddress = 0;
    Serial.println("INA226 ok=0");
    return false;
}

Power readPower() {
    Power p;
    if (inaAddress == 0) return p;
    float shuntVolts = (int16_t)inaRead(KInaRegShunt) * 2.5e-6f;
    p.volts      = inaRead(KInaRegBus) * 1.25e-3f;
    p.milliamps  = shuntVolts / KShuntOhm * 1000.0f;
    p.milliwatts = p.volts * p.milliamps;
    return p;
}

// ---------------------------------------------------------------------------
//  Wi-Fi
// ---------------------------------------------------------------------------
bool connectToWifi() {
    if (!credentials.hasRouter()) {
        Serial.println("WIFI_CONNECT_FAIL reason=NO_CREDENTIALS (reset and press c)");
        showOnLcd("No WiFi config", "Reset, press c");
        return false;
    }
    showOnLcd("Connecting WiFi", "");
    WiFi.disconnect();
    WiFi.begin(credentials.routerSsid.c_str(), credentials.routerPassword.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < KWifiTimeoutMs) delay(250);
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("WIFI_CONNECT_FAIL status=%d\n", (int)WiFi.status());
        showOnLcd("WiFi FAIL", "");
        return false;
    }
    WiFi.setSleep(false);                         // NFR-RT-2
    Serial.printf("WIFI_STATUS connected=1 hostname=%s ip=%s rssi=%d channel=%d\n",
                  WiFi.getHostname(), WiFi.localIP().toString().c_str(),
                  WiFi.RSSI(), WiFi.channel());
    return true;
}

// =============================================================================
//  Echo server role
// =============================================================================

// SEC-7: answer only senders on our own subnet.
bool isLocalSender(IPAddress remote) {
    uint32_t mask = (uint32_t)WiFi.subnetMask();
    return ((uint32_t)remote & mask) == ((uint32_t)WiFi.localIP() & mask);
}

void handleEchoPacket() {
    int size = udp.parsePacket();
    if (size <= 0) return;

    IPAddress remoteIp   = udp.remoteIP();
    uint16_t  remotePort = udp.remotePort();

    if (size > (int)KMaxPacketBytes || !isLocalSender(remoteIp)) {
        udp.flush();                              // drop it
        rejectedPackets++;
        return;
    }

    int length = udp.read(rxBuffer, KMaxPacketBytes);

    // Status request: uptime + own power (FR-BW-6, FR-BW-7).
    if (length == 7 && memcmp(rxBuffer, "STATUS?", 7) == 0) {
        Power p = readPower();
        char reply[128];
        int n = snprintf(reply, sizeof(reply),
                         "STATUS uptime_ms=%lu v=%.3f i_ma=%.1f p_mw=%.0f echoed=%lu",
                         (unsigned long)millis(), p.volts, p.milliamps, p.milliwatts,
                         (unsigned long)echoedPackets);
        udp.beginPacket(remoteIp, remotePort);
        udp.write((const uint8_t*)reply, n);
        udp.endPacket();
        return;
    }

    // Echo: send back unchanged (FR-BW-1).
    udp.beginPacket(remoteIp, remotePort);
    udp.write(rxBuffer, length);
    udp.endPacket();
    echoedPackets++;
}

void echoLoop() {
    handleEchoPacket();
    if (millis() - lastPowerMillis >= KPowerIntervalMs) {
        lastPowerMillis = millis();
        Power p = readPower();
        Serial.printf("POWER uptime_s=%lu v=%.3f i_ma=%.1f p_mw=%.0f echoed=%lu rejected=%lu\n",
                      (unsigned long)(millis() / 1000), p.volts, p.milliamps, p.milliwatts,
                      (unsigned long)echoedPackets, (unsigned long)rejectedPackets);
    }
}

// =============================================================================
//  Controller role
// =============================================================================

void findEchoServer() {
    IPAddress found = MDNS.queryHost(KEchoName, 2000);
    bool viaMdns = (uint32_t)found != 0;
    echoIp = viaMdns ? found : KEchoFallbackIp;
    Serial.printf("ECHO_TARGET ip=%s via=%s\n", echoIp.toString().c_str(),
                  viaMdns ? "mdns" : "fallback");
}

void drainUdp() {                                 // throw away late replies
    while (udp.parsePacket() > 0) udp.flush();
}

EchoStatus requestStatus() {
    EchoStatus s;
    for (int attempt = 0; attempt < 3 && !s.ok; attempt++) {
        drainUdp();
        udp.beginPacket(echoIp, KEchoPort);
        udp.write((const uint8_t*)"STATUS?", 7);
        udp.endPacket();
        int64_t start = esp_timer_get_time();
        while (esp_timer_get_time() - start < KTimeoutUs) {
            if (udp.parsePacket() <= 0) continue;
            int n = udp.read(rxBuffer, sizeof(rxBuffer) - 1);
            if (n < 0) n = 0;
            rxBuffer[n] = 0;
            unsigned long up = 0;
            if (sscanf((char*)rxBuffer, "STATUS uptime_ms=%lu v=%f i_ma=%f p_mw=%f",
                       &up, &s.power.volts, &s.power.milliamps, &s.power.milliwatts) == 4) {
                s.uptimeMs = up;
                s.ok = true;
                break;
            }
        }
    }
    return s;
}

void printStatus(const char* when, const EchoStatus& s) {
    if (!s.ok) { Serial.printf("ECHO_STATUS when=%s ok=0\n", when); return; }
    Serial.printf("ECHO_STATUS when=%s uptime_ms=%lu v=%.3f i_ma=%.1f p_mw=%.0f\n", when,
                  (unsigned long)s.uptimeMs, s.power.volts, s.power.milliamps, s.power.milliwatts);
}

// Sends one packet and waits for the same packet back.
// Returns the round-trip time in µs, or 0 if lost or changed.
uint32_t sendOne(uint32_t seq, uint16_t size) {
    memcpy(txBuffer, &seq, 4);                    // sequence number first
    for (uint16_t i = 4; i < size; i++) txBuffer[i] = (uint8_t)(seq + i);

    drainUdp();
    int64_t start = esp_timer_get_time();         // NFR-RT-3
    udp.beginPacket(echoIp, KEchoPort);
    udp.write(txBuffer, size);
    udp.endPacket();

    while (esp_timer_get_time() - start < KTimeoutUs) {
        if (udp.parsePacket() <= 0) continue;
        int n = udp.read(rxBuffer, sizeof(rxBuffer));
        uint32_t rxSeq = 0;
        if (n >= 4) memcpy(&rxSeq, rxBuffer, 4);
        if (rxSeq != seq) continue;               // late reply to an earlier packet
        if (n != size || memcmp(rxBuffer, txBuffer, size) != 0) return 0;  // changed
        uint32_t rtt = (uint32_t)(esp_timer_get_time() - start);
        return rtt == 0 ? 1 : rtt;
    }
    return 0;                                     // timeout = lost
}

void runSeries() {
    Serial.println("BW_SERIES start=1");
    showOnLcd("Bandwidth test", "running...");

    EchoStatus before = requestStatus();
    printStatus("before", before);

    uint32_t seq = 1;
    for (int i = 0; i < KWarmupPackets; i++) sendOne(seq++, 32);

    for (uint16_t size : KSizes) {
        int received = 0, samples = 0;
        Power sum;

        for (int k = 0; k < KPacketsPerSize; k++) {
            uint32_t rtt = sendOne(seq++, size);
            if (rtt > 0) rtts[received++] = rtt;
            if (k % 20 == 0) {                    // own power, 5 samples per size
                Power p = readPower();
                sum.volts += p.volts; sum.milliamps += p.milliamps; sum.milliwatts += p.milliwatts;
                samples++;
            }
        }

        int lost = KPacketsPerSize - received;
        uint32_t minUs = 0, medUs = 0, maxUs = 0;
        float avgUs = 0, kbps = 0;
        if (received > 0) {
            std::sort(rtts, rtts + received);
            minUs = rtts[0];
            maxUs = rtts[received - 1];
            medUs = rtts[received / 2];
            uint64_t total = 0;
            for (int i = 0; i < received; i++) total += rtts[i];
            avgUs = (float)total / received;
            kbps  = 2.0f * size * 8.0f / avgUs * 1000.0f;   // bits/µs -> kbit/s
        }
        Serial.printf("BW_RESULT size=%u sent=%d lost=%d rtt_min_us=%lu rtt_med_us=%lu "
                      "rtt_avg_us=%.0f rtt_max_us=%lu kbps=%.0f a_v=%.3f a_i_ma=%.1f a_p_mw=%.0f\n",
                      size, KPacketsPerSize, lost, (unsigned long)minUs, (unsigned long)medUs,
                      avgUs, (unsigned long)maxUs, kbps,
                      sum.volts / samples, sum.milliamps / samples, sum.milliwatts / samples);

        char line1[17], line2[17];
        snprintf(line1, sizeof(line1), "%uB %.1fms", size, medUs / 1000.0f);
        snprintf(line2, sizeof(line2), "L%d %.0fkbps", lost, kbps);
        showOnLcd(line1, line2);
    }

    EchoStatus after = requestStatus();
    printStatus("after", after);
    bool restarted = before.ok && after.ok && after.uptimeMs < before.uptimeMs;   // FR-BW-6
    Serial.printf("BW_SERIES done=1 echo_restarted=%d rssi=%d\n", restarted ? 1 : 0, WiFi.RSSI());
    Serial.println("Send r to run again.");
    showOnLcd(restarted ? "Done: ECHO RESET" : "Series done", "r = run again");
}

void controllerLoop() {
    if (!Serial.available()) return;
    char c = Serial.read();
    if (c != 'r' && c != 'R') return;
    findEchoServer();
    runSeries();
}

// =============================================================================
//  Setup and loop
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(KPinRole, INPUT_PULLUP);
    isEchoServer = digitalRead(KPinRole) == LOW;
    const char* hostName = isEchoServer ? KEchoName : KControllerName;
    Serial.printf("ROLE role=%s\n", isEchoServer ? "echo" : "controller");

    Wire.begin(KBusSda, KBusScl, KBusHz);
    if (!isEchoServer) {
        lcd.begin(16, 2);
        lcd.setBacklight(255);
        showOnLcd("Lab 8 bandwidth", "Starting...");
    }
    setupIna();

    credentials = lab8::loadCredentials();
    lab8::printCredentialStatus(credentials);
    showOnLcd("WiFi setup:", "send c on Serial");
    if (!credentials.hasRouter() || lab8::setupRequested()) {
        lab8::runSetupMenu();
        credentials = lab8::loadCredentials();
        lab8::printCredentialStatus(credentials);
    }

    WiFi.setHostname(hostName);                   // before WiFi starts
    WiFi.mode(WIFI_STA);
    if (!connectToWifi()) return;
    MDNS.begin(hostName);

    if (isEchoServer) {
        udp.begin(KEchoPort);
        Serial.printf("ECHO_READY port=%u\n", KEchoPort);
    } else {
        udp.begin(KLocalPort);
        findEchoServer();
        runSeries();
    }
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        if (!connectToWifi()) { delay(5000); return; }
        udp.begin(isEchoServer ? KEchoPort : KLocalPort);
    }
    if (isEchoServer) echoLoop();
    else              controllerLoop();
    delay(isEchoServer ? 0 : 20);
}
