/*
 * =============================================================================
 *  Lab 8 - 8.6 Bandwidth test + movement monitor: one firmware, role by jumper
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
 *    else -> sent back unchanged. Only the local subnet, max 1460 bytes (SEC-7).
 *    POWER line every 5 s.
 *
 *  Controller
 *  ----------
 *    Finds the echo server (mDNS lab8-echo.local, else KEchoFallbackIp), runs
 *    one series at boot and again on 'r':
 *      - echo server STATUS before and after (restart check, its power)
 *      - sizes 32...1460 bytes, 100 packets each, one at a time, 500 ms
 *        timeout, sequence number + content checked
 *      - min / median / average / max RTT (µs, esp_timer), losses,
 *        throughput, own power; each size shown on the LCD
 *    Throughput = 2 x size x 8 / average RTT  (bits both ways per round trip)
 *
 *  Movement monitor (phase 2a light, no reflector)
 *  -----------------------------------------------
 *    Both boards read the router's RSSI every 100 ms (the router sends about
 *    ten beacons per second). Every 1 s window gives a median and a spread
 *    (standard deviation, dB). Board B also samples its PIR (GPIO 14).
 *    Controller: 'm' starts/stops the monitor. Every second it asks B
 *    "MEAS?" and B answers with its last window, PIR state and power.
 *    A prints one MOVE line per second with both links; a link is flagged
 *    as movement when its spread is above KMoveSpreadDb. B's PIR is the
 *    reference for real movement.
 *
 *  Movement web page (controller)
 *  ------------------------------
 *    http://lab8-esp32.local/ (or http://<A's ip>/) shows live graphs of the
 *    last 3 minutes: RSSI of both links, their spread with the threshold, and
 *    B's PIR as shaded bands; boxes show which link reports movement now.
 *    /api/move returns the same data as JSON. Read-only, local subnet only,
 *    script only from the ESP32 itself (CSP). The monitor starts by itself
 *    after the boot series.
 *
 *  Empty-room calibration (FR-RADAR-4)
 *  ------------------------------------
 *    'k' on A: 30 s to leave (nobody within about 2 m of a board, nobody on
 *    the paths), then 30 s of recording. Per link: baseline level = median of
 *    the 1 s medians, calm spread = largest spread seen. After that a link
 *    reports movement when its level is >= 3 dB below the baseline, or its
 *    spread is above the calm spread + 1 dB. Before calibration the fixed
 *    2.0 dB spread threshold is used. 'k' again re-calibrates.
 *
 *  Requirements: FR-BW-1...4, FR-BW-6, FR-BW-7, NFR-RT-2, NFR-RT-3, SEC-7,
 *  FR-RADAR-4 (first part, without empty-room calibration), HW-11.
 *  Not yet: direct mode (FR-BW-5), web page output (FR-BW-4).
 *
 *  Serial protocol
 *  ---------------
 *    ROLE role=<echo|controller>
 *    INA226 ok=<0|1> addr=<0x4?>
 *    WIFI_STATUS connected=1 hostname=<name> ip=<ip> rssi=<dBm> channel=<n>
 *    echo server:
 *      ECHO_READY port=5001
 *      POWER uptime_s=<s> v=<V> i_ma=<mA> p_mw=<mW> echoed=<n> rejected=<n> pir=<0|1>
 *    controller:
 *      ECHO_TARGET ip=<ip> via=<mdns|fallback>
 *      ECHO_STATUS when=<before|after> uptime_ms=<ms> v=<V> i_ma=<mA> p_mw=<mW>
 *      BW_RESULT size=<B> sent=<n> lost=<n> changed=<n> rtt_min_us=.. rtt_med_us=..
 *                rtt_avg_us=.. rtt_max_us=.. kbps=.. a_v=.. a_i_ma=.. a_p_mw=..
 *      BW_SERIES done=1 echo_restarted=<0|1> rssi=<dBm>
 *      CAL state=<leave|recording|done> ... a_base=<dBm> a_calm=<dB> b_base=.. b_calm=..
 *      MOVE uptime_s=<s> a_rssi=<dBm> a_sd=<dB> a_drop=<dB> a_move=<0|1>
 *           b_ok=<0|1> b_rssi=<dBm> b_sd=<dB> b_drop=<dB> b_move=<0|1>
 *           b_pir=<0|1> b_pir_any=<0|1> b_pir_edges=<n> b_p_mw=<mW>
 *    B's answer to MEAS?:
 *      MEAS rssi=<dBm> sd=<dB> n=<samples> pir=<0|1> pir_any=<0|1>
 *           pir_edges=<n> v=<V> i_ma=<mA> p_mw=<mW>
 *
 *  platformio.ini:  build_src_filter = -<*> +<lab8_bandwidth.cpp>
 * =============================================================================
 */

#include <Arduino.h>
#include <ESPmDNS.h>
#include <LiquidCrystal_PCF8574.h>
#include <WiFi.h>
#include <WebServer.h>
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
const IPAddress       KEchoFallbackIp(192, 168, 32, 6);   // B's DHCP address until .51 is reserved
constexpr uint16_t    KEchoPort  = 5001;
constexpr uint16_t    KLocalPort = 5002;
constexpr size_t      KMaxPacketBytes = 1460;    // Arduino UDP receive buffer limit (1472 is cut to 1460)

constexpr uint16_t KSizes[]        = {32, 64, 128, 256, 512, 1024, 1460};
constexpr int      KPacketsPerSize = 100;
constexpr uint32_t KTimeoutUs      = 500000;     // 500 ms = lost
constexpr int      KWarmupPackets  = 5;          // fills ARP tables, not counted

constexpr uint8_t  KPinPir         = 14;          // HC-SR501 OUT, board B only (A has DHT11 here)
constexpr uint32_t KSampleMs       = 100;         // RSSI/PIR sample interval
constexpr int      KWindowSamples  = 10;          // 10 x 100 ms = 1 s window
constexpr float    KMoveSpreadDb   = 2.0f;        // spread above this = movement (tune)
constexpr uint32_t KMeasTimeoutUs  = 300000;      // wait for B's MEAS answer
constexpr int      KCalLeaveS      = 30;          // time to walk away after 'k'
constexpr int      KCalRecordS     = 30;          // empty-room recording
constexpr float    KCalDropDb      = 3.0f;        // level this far below baseline = movement
constexpr float    KCalSpreadMarginDb = 1.0f;     // spread above calm spread + this = movement

constexpr uint16_t KWebPort         = 80;
constexpr int      KHistorySize     = 180;        // 180 x 1 s = 3 minutes on the page

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
int       changedPackets = 0;                 // returned, but wrong length or content

uint32_t echoedPackets   = 0;
uint32_t rejectedPackets = 0;
uint32_t lastPowerMillis = 0;

struct Power { float volts = 0, milliamps = 0, milliwatts = 0; };
struct EchoStatus { bool ok = false; uint32_t uptimeMs = 0; Power power; };

// One 1 s window of RSSI samples (and PIR on board B).
struct Window {
    int      samples[KWindowSamples];
    int      count = 0;
    bool     pirAny = false;                      // PIR high at any sample
    int      pirEdges = 0;                        // 0 -> 1 changes
    int      lastPir = 0;
    uint32_t lastSampleMs = 0;
};
struct WindowResult {
    bool  valid = false;
    int   median = 0;
    float spread = 0;                             // standard deviation, dB
    int   n = 0;
    bool  pirAny = false;
    int   pirEdges = 0;
};

Window       window;                              // this board's current window
WindowResult lastWindow;                          // this board's last finished window
bool         monitorOn = false;                   // controller: movement monitor running

// One second of the movement monitor, kept for the web page.
struct MoveSample {
    uint32_t uptimeS;
    int8_t   aRssi, bRssi;
    float    aSpread, bSpread;
    bool     aMove, bMove, bOk, bPirAny;
};
MoveSample history[KHistorySize];
int        historyCount = 0;                      // samples stored (max KHistorySize)
int        historyNext  = 0;                      // where the next sample goes

// Empty-room calibration (controller).
enum CalState { CAL_NONE, CAL_LEAVE, CAL_RECORD, CAL_DONE };
CalState calState = CAL_NONE;
int      calSecondsLeft = 0;
int      calCount = 0, calCountB = 0;
int8_t   calA[KCalRecordS], calB[KCalRecordS];    // 1 s medians while recording
float    calCalmA = 0, calCalmB = 0;              // largest spread while recording
float    baseA = 0, baseB = 0;                    // baseline levels (dBm)

WebServer server(KWebPort);
bool      webStarted = false;

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
                  (int)WiFi.RSSI(), (int)WiFi.channel());
    return true;
}

// =============================================================================
//  RSSI / PIR window (both roles)
// =============================================================================

WindowResult finishWindow(Window& w) {
    WindowResult r;
    r.n = w.count;
    r.pirAny = w.pirAny;
    r.pirEdges = w.pirEdges;
    if (w.count > 0) {
        int sorted[KWindowSamples];
        memcpy(sorted, w.samples, sizeof(int) * w.count);
        std::sort(sorted, sorted + w.count);
        r.median = sorted[w.count / 2];
        float mean = 0;
        for (int i = 0; i < w.count; i++) mean += w.samples[i];
        mean /= w.count;
        float var = 0;
        for (int i = 0; i < w.count; i++) var += (w.samples[i] - mean) * (w.samples[i] - mean);
        r.spread = sqrtf(var / w.count);
        r.valid = true;
    }
    w.count = 0;
    w.pirAny = false;
    w.pirEdges = 0;
    return r;
}

// Takes one sample every 100 ms. Returns true when a 1 s window is finished
// (the result is then in lastWindow).
bool sampleWindow() {
    if (millis() - window.lastSampleMs < KSampleMs) return false;
    window.lastSampleMs = millis();

    window.samples[window.count++] = WiFi.RSSI();   // from the router's frames
    if (isEchoServer) {
        int pir = digitalRead(KPinPir);
        if (pir) window.pirAny = true;
        if (pir && !window.lastPir) window.pirEdges++;
        window.lastPir = pir;
    }
    if (window.count < KWindowSamples) return false;
    lastWindow = finishWindow(window);
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
        udp.clear();                              // drop it
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

    // Measurement request: last RSSI window + PIR + power (phase 2a).
    if (length == 5 && memcmp(rxBuffer, "MEAS?", 5) == 0) {
        Power p = readPower();
        char reply[192];
        int n = snprintf(reply, sizeof(reply),
                         "MEAS rssi=%d sd=%.2f n=%d pir=%d pir_any=%d pir_edges=%d "
                         "v=%.3f i_ma=%.1f p_mw=%.0f",
                         lastWindow.median, lastWindow.spread, lastWindow.n,
                         digitalRead(KPinPir), lastWindow.pirAny ? 1 : 0, lastWindow.pirEdges,
                         p.volts, p.milliamps, p.milliwatts);
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
    sampleWindow();
    if (millis() - lastPowerMillis >= KPowerIntervalMs) {
        lastPowerMillis = millis();
        Power p = readPower();
        Serial.printf("POWER uptime_s=%lu v=%.3f i_ma=%.1f p_mw=%.0f echoed=%lu rejected=%lu pir=%d\n",
                      (unsigned long)(millis() / 1000), p.volts, p.milliamps, p.milliwatts,
                      (unsigned long)echoedPackets, (unsigned long)rejectedPackets,
                      digitalRead(KPinPir));
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
    while (udp.parsePacket() > 0) udp.clear();
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
        if (n != size || memcmp(rxBuffer, txBuffer, size) != 0) { changedPackets++; return 0; }
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
    if (!before.ok) {                             // no echo server: don't wait 6 min for timeouts
        Serial.println("BW_SERIES done=0 reason=NO_ECHO_SERVER (check B's power, then send r)");
        showOnLcd("No echo server", "r=retry m=move");
        return;
    }

    uint32_t seq = 1;
    for (int i = 0; i < KWarmupPackets; i++) sendOne(seq++, 32);

    for (uint16_t size : KSizes) {
        int received = 0, samples = 0;
        changedPackets = 0;
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
        Serial.printf("BW_RESULT size=%u sent=%d lost=%d changed=%d rtt_min_us=%lu rtt_med_us=%lu "
                      "rtt_avg_us=%.0f rtt_max_us=%lu kbps=%.0f a_v=%.3f a_i_ma=%.1f a_p_mw=%.0f\n",
                      size, KPacketsPerSize, lost, changedPackets, (unsigned long)minUs, (unsigned long)medUs,
                      avgUs, (unsigned long)maxUs, kbps,
                      sum.volts / samples, sum.milliamps / samples, sum.milliwatts / samples);

        char line1[40], line2[40];                   // showOnLcd() cuts to 16
        snprintf(line1, sizeof(line1), "%uB %.1fms", size, medUs / 1000.0f);
        snprintf(line2, sizeof(line2), "L%d %.0fkbps", lost, kbps);
        showOnLcd(line1, line2);
    }

    EchoStatus after = requestStatus();
    printStatus("after", after);
    bool restarted = before.ok && after.ok && after.uptimeMs < before.uptimeMs;   // FR-BW-6
    Serial.printf("BW_SERIES done=1 echo_restarted=%d rssi=%d\n", restarted ? 1 : 0, (int)WiFi.RSSI());
    Serial.println("Send r to run again, m for the movement monitor, k to calibrate.");
    showOnLcd(restarted ? "Done: ECHO RESET" : "Series done", "r=again m=move");
}

// =============================================================================
//  Empty-room calibration (FR-RADAR-4)
// =============================================================================
void startCalibration() {
    calState = CAL_LEAVE;
    calSecondsLeft = KCalLeaveS;
    calCount = calCountB = 0;
    calCalmA = calCalmB = 0;
    Serial.printf("CAL state=leave seconds=%d (nobody within 2 m of a board, nobody on the paths)\n", KCalLeaveS);
}

int8_t medianOf(int8_t* v, int n) {
    std::sort(v, v + n);
    return v[n / 2];
}

// Called once per second with the newest windows of both links.
void stepCalibration(int aRssi, float aSd, bool bOk, int bRssi, float bSd) {
    if (calState == CAL_LEAVE) {
        if (--calSecondsLeft > 0) return;
        calState = CAL_RECORD;
        calSecondsLeft = KCalRecordS;
        Serial.printf("CAL state=recording seconds=%d\n", KCalRecordS);
        return;
    }
    if (calState != CAL_RECORD) return;

    calA[calCount++] = aRssi;
    if (aSd > calCalmA) calCalmA = aSd;
    if (bOk) {
        calB[calCountB++] = bRssi;
        if (bSd > calCalmB) calCalmB = bSd;
    }
    if (--calSecondsLeft > 0) return;

    baseA = medianOf(calA, calCount);
    baseB = calCountB > 0 ? medianOf(calB, calCountB) : 0;
    calState = CAL_DONE;
    Serial.printf("CAL state=done a_base=%.0f a_calm=%.2f b_base=%.0f b_calm=%.2f b_seconds=%d "
                  "rule: drop>=%.1f dB or spread>calm+%.1f dB\n",
                  baseA, calCalmA, baseB, calCalmB, calCountB, KCalDropDb, KCalSpreadMarginDb);
    if (calCountB < KCalRecordS / 2) Serial.println("CAL warning: B answered less than half the time, run k again");
}

// Asks B for its last window and prints one MOVE line with both links.
void reportMovement() {
    drainUdp();
    udp.beginPacket(echoIp, KEchoPort);
    udp.write((const uint8_t*)"MEAS?", 5);
    udp.endPacket();

    bool bOk = false;
    int bRssi = 0, bN = 0, bPir = 0, bPirAny = 0, bEdges = 0;
    float bSd = 0, bV = 0, bI = 0, bP = 0;
    int64_t start = esp_timer_get_time();
    while (esp_timer_get_time() - start < KMeasTimeoutUs) {
        if (udp.parsePacket() <= 0) continue;
        int n = udp.read(rxBuffer, sizeof(rxBuffer) - 1);
        if (n < 0) n = 0;
        rxBuffer[n] = 0;
        if (sscanf((char*)rxBuffer,
                   "MEAS rssi=%d sd=%f n=%d pir=%d pir_any=%d pir_edges=%d v=%f i_ma=%f p_mw=%f",
                   &bRssi, &bSd, &bN, &bPir, &bPirAny, &bEdges, &bV, &bI, &bP) == 9) {
            bOk = bN > 0;
            break;
        }
    }

    stepCalibration(lastWindow.median, lastWindow.spread, bOk, bRssi, bSd);

    float aDrop = 0, bDrop = 0;
    bool aMove, bMove;
    if (calState == CAL_DONE) {
        aDrop = baseA - lastWindow.median;        // positive = weaker than the empty room
        bDrop = baseB - bRssi;
        aMove = aDrop >= KCalDropDb || lastWindow.spread > calCalmA + KCalSpreadMarginDb;
        bMove = bOk && (bDrop >= KCalDropDb || bSd > calCalmB + KCalSpreadMarginDb);
    } else {
        aMove = lastWindow.spread > KMoveSpreadDb;
        bMove = bOk && bSd > KMoveSpreadDb;
    }
    Serial.printf("MOVE uptime_s=%lu a_rssi=%d a_sd=%.2f a_drop=%.1f a_move=%d "
                  "b_ok=%d b_rssi=%d b_sd=%.2f b_drop=%.1f b_move=%d b_pir=%d b_pir_any=%d b_pir_edges=%d b_p_mw=%.0f\n",
                  (unsigned long)(millis() / 1000), lastWindow.median, lastWindow.spread, aDrop, aMove ? 1 : 0,
                  bOk ? 1 : 0, bRssi, bSd, bDrop, bMove ? 1 : 0, bPir, bPirAny, bEdges, bP);

    MoveSample& h = history[historyNext];
    h.uptimeS = millis() / 1000;
    h.aRssi = lastWindow.median;  h.aSpread = lastWindow.spread;  h.aMove = aMove;
    h.bRssi = bRssi;              h.bSpread = bSd;                h.bMove = bMove;
    h.bOk = bOk;                  h.bPirAny = bOk && bPirAny;
    historyNext = (historyNext + 1) % KHistorySize;
    if (historyCount < KHistorySize) historyCount++;

    char line1[40], line2[40];                   // showOnLcd() cuts to 16
    snprintf(line1, sizeof(line1), "A%d s%.1f %s", lastWindow.median, lastWindow.spread, aMove ? "MOVE" : "");
    if (bOk) snprintf(line2, sizeof(line2), "B%d s%.1f P%d%s", bRssi, bSd, bPirAny, bMove ? " M" : "");
    else     snprintf(line2, sizeof(line2), "B no answer");
    if (calState == CAL_LEAVE)  { snprintf(line1, sizeof(line1), "Calibrate: leave"); snprintf(line2, sizeof(line2), "starts in %d s", calSecondsLeft); }
    if (calState == CAL_RECORD) { snprintf(line1, sizeof(line1), "Calibrating..."); snprintf(line2, sizeof(line2), "keep away %d s", calSecondsLeft); }
    showOnLcd(line1, line2);
}

// =============================================================================
//  Movement web page (controller)
// =============================================================================

const char KPageHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lab 8 movement</title>
<style>
:root{--bg:#f6f5f2;--card:#fff;--ink:#1d1d1b;--mute:#6b6a66;--line:#e2e0da;
--a:#2563eb;--b:#d97706;--pir:rgba(120,120,120,.16);--move:#dc2626}
@media (prefers-color-scheme:dark){:root{--bg:#141413;--card:#1f1f1d;--ink:#eceae4;
--mute:#9c9a93;--line:#34332f;--a:#60a5fa;--b:#fbbf24;--pir:rgba(200,200,200,.12);--move:#f87171}}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.4 system-ui,sans-serif}
main{max-width:960px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:0 0 4px}p{margin:0 0 12px;color:var(--mute)}
.boxes{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-bottom:12px}
.box{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px}
.box b{display:block;font-size:13px;color:var(--mute);font-weight:500}
.box span{font-size:20px;font-weight:600}
.on{color:var(--move)}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px;margin-bottom:12px}
.card h2{font-size:14px;margin:0 0 6px;font-weight:600}
canvas{width:100%;height:200px;display:block}
.key{font-size:13px;color:var(--mute)}.key i{display:inline-block;width:12px;height:3px;margin:0 4px 3px 10px;vertical-align:middle}
</style></head><body><main>
<h1>Lab 8 &ndash; movement monitor</h1>
<p id="info">Waiting for data&hellip;</p>
<div class="boxes">
<div class="box"><b>Link A &ndash; router</b><span id="ma">&ndash;</span></div>
<div class="box"><b>Link B &ndash; router</b><span id="mb">&ndash;</span></div>
<div class="box"><b>PIR at B</b><span id="mp">&ndash;</span></div>
</div>
<div class="card"><h2>Signal strength (dBm, median per second)</h2><canvas id="c1"></canvas>
<div class="key"><i style="background:var(--a)"></i>A&ndash;router<i style="background:var(--b)"></i>B&ndash;router<i style="background:var(--pir);height:10px"></i>PIR at B saw movement &middot; dashed = empty-room baseline</div></div>
<div class="card"><h2>Spread (dB, standard deviation per second)</h2><canvas id="c2"></canvas>
<div class="key"><i style="background:var(--a)"></i>A<i style="background:var(--b)"></i>B &middot; dashed = movement threshold</div></div>
</main><script src="/app.js"></script></body></html>
)HTML";

const char KPageJs[] PROGMEM = R"JS(
const css=n=>getComputedStyle(document.documentElement).getPropertyValue(n).trim();
function draw(id,rows,ia,ib,lo,hi,thr,refs){
  const c=document.getElementById(id),r=window.devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;
  c.width=w*r;c.height=h*r;const g=c.getContext('2d');g.scale(r,r);g.clearRect(0,0,w,h);
  const L=36,R=w-6,T=6,B=h-18,n=180,x=i=>L+(R-L)*i/(n-1),y=v=>B-(B-T)*(v-lo)/(hi-lo);
  const off=n-rows.length;
  g.fillStyle=css('--pir');rows.forEach((s,i)=>{if(s[8])g.fillRect(x(i+off)-(R-L)/n/2,T,(R-L)/n+1,B-T)});
  g.strokeStyle=css('--line');g.fillStyle=css('--mute');g.font='11px system-ui';g.lineWidth=1;
  for(let k=0;k<=4;k++){const v=lo+(hi-lo)*k/4,yy=y(v);g.beginPath();g.moveTo(L,yy);g.lineTo(R,yy);g.stroke();g.fillText(v.toFixed(hi-lo<10?1:0),2,yy+4)}
  g.fillText('-3 min',L,h-4);g.fillText('now',R-22,h-4);
  if(thr!=null){g.strokeStyle=css('--move');g.setLineDash([4,4]);g.beginPath();g.moveTo(L,y(thr));g.lineTo(R,y(thr));g.stroke();g.setLineDash([])}
  (refs||[]).forEach(([v,col])=>{g.strokeStyle=css(col);g.lineWidth=1;g.setLineDash([5,4]);g.beginPath();g.moveTo(L,y(v));g.lineTo(R,y(v));g.stroke();g.setLineDash([])});
  [[ia,'--a'],[ib,'--b']].forEach(([k,col])=>{g.strokeStyle=css(col);g.lineWidth=2;g.beginPath();let up=false;
    rows.forEach((s,i)=>{if(k>=5&&!s[4]){up=false;return}const v=Math.max(lo,Math.min(hi,s[k]));
      if(up)g.lineTo(x(i+off),y(v));else{g.moveTo(x(i+off),y(v));up=true}});g.stroke()});
}
function setBox(id,on,text){const e=document.getElementById(id);e.textContent=text;e.className=on?'on':''}
async function tick(){
  try{
    const d=await (await fetch('/api/move',{cache:'no-store'})).json(),rows=d.rows;
    if(!rows.length){document.getElementById('info').textContent=d.monitor?'Monitor running, waiting for the first second':'Monitor is off (send m on Serial)';return}
    const s=rows[rows.length-1];
    const cal=d.cal==='done'?'calibrated: movement = level '+d.drop+' dB below the dashed baseline, or spread above the dashed line'
      :d.cal==='leave'?'CALIBRATION: leave the area, recording starts in '+d.calLeft+' s'
      :d.cal==='recording'?'CALIBRATION: recording the empty room, keep away '+d.calLeft+' s'
      :'not calibrated (send k): movement = spread above '+d.threshold+' dB';
    document.getElementById('info').textContent='Uptime '+s[0]+' s \u00b7 '+cal;
    setBox('ma',s[3],s[3]?'MOVEMENT':'quiet');
    setBox('mb',s[7],!s[4]?'no answer':(s[7]?'MOVEMENT':'quiet'));
    setBox('mp',s[8],!s[4]?'–':(s[8]?'movement':'quiet'));
    const done=d.cal==='done';
    let lo=0,hi=-100;rows.forEach(r=>{lo=Math.min(lo,r[1],r[4]?r[5]:0);hi=Math.max(hi,r[1],r[4]?r[5]:-100)});
    if(done){lo=Math.min(lo,d.baseA-d.drop,d.baseB-d.drop);hi=Math.max(hi,d.baseA,d.baseB)}
    draw('c1',rows,1,5,Math.floor(lo/5)*5-5,Math.ceil(hi/5)*5+5,null,done?[[d.baseA,'--a'],[d.baseB,'--b']]:[]);
    let sm=d.threshold+1;rows.forEach(r=>{sm=Math.max(sm,r[2],r[6])});if(done)sm=Math.max(sm,d.thrA,d.thrB);
    draw('c2',rows,2,6,0,Math.ceil(sm),done?null:d.threshold,done?[[d.thrA,'--a'],[d.thrB,'--b']]:[]);
  }catch(e){document.getElementById('info').textContent='No contact with the ESP32'}
}
tick();setInterval(tick,1000);addEventListener('resize',tick);
)JS";

void sendSecurityHeaders() {
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("X-Content-Type-Options", "nosniff");
    server.sendHeader("X-Frame-Options", "DENY");
    server.sendHeader("Referrer-Policy", "no-referrer");
    // Script only from this ESP32 (/app.js), data only from /api/move.
    server.sendHeader("Content-Security-Policy",
                      "default-src 'none'; script-src 'self'; connect-src 'self'; "
                      "style-src 'unsafe-inline'");
}

// Answer only clients on our own subnet (same rule as the echo server, SEC-7).
bool acceptRequest() {
    sendSecurityHeaders();
    if (!isLocalSender(server.client().remoteIP())) {
        server.send(403, "text/plain", "Forbidden: local network only\n");
        return false;
    }
    return true;
}

void handlePage() { if (acceptRequest()) server.send_P(200, "text/html; charset=utf-8", KPageHtml); }
void handleJs()   { if (acceptRequest()) server.send_P(200, "text/javascript; charset=utf-8", KPageJs); }

// rows: [uptime_s, a_rssi, a_sd, a_move, b_ok, b_rssi, b_sd, b_move, b_pir_any], oldest first.
void handleApiMove() {
    if (!acceptRequest()) return;
    String json;
    json.reserve(200 + historyCount * 44);
    json += "{\"monitor\":";
    json += monitorOn ? "true" : "false";
    json += ",\"threshold\":";
    json += String(KMoveSpreadDb, 1);
    json += ",\"cal\":\"";
    json += calState == CAL_DONE ? "done" : calState == CAL_RECORD ? "recording" : calState == CAL_LEAVE ? "leave" : "none";
    json += "\",\"calLeft\":";
    json += String(calSecondsLeft);
    if (calState == CAL_DONE) {
        json += ",\"baseA\":";  json += String(baseA, 0);
        json += ",\"baseB\":";  json += String(baseB, 0);
        json += ",\"thrA\":";   json += String(calCalmA + KCalSpreadMarginDb, 2);
        json += ",\"thrB\":";   json += String(calCalmB + KCalSpreadMarginDb, 2);
        json += ",\"drop\":";   json += String(KCalDropDb, 1);
    }
    json += ",\"rows\":[";
    int first = (historyNext - historyCount + KHistorySize) % KHistorySize;
    char item[64];
    for (int i = 0; i < historyCount; i++) {
        const MoveSample& h = history[(first + i) % KHistorySize];
        snprintf(item, sizeof(item), "%s[%lu,%d,%.2f,%d,%d,%d,%.2f,%d,%d]", i ? "," : "",
                 (unsigned long)h.uptimeS, h.aRssi, h.aSpread, h.aMove, h.bOk,
                 h.bRssi, h.bSpread, h.bMove, h.bPirAny);
        json += item;
    }
    json += "]}";
    server.send(200, "application/json", json);
}

void handleNotFound() {
    sendSecurityHeaders();
    server.send(404, "text/plain", "Not found\n");
}

void startWebServer() {
    server.on("/", HTTP_GET, handlePage);
    server.on("/app.js", HTTP_GET, handleJs);
    server.on("/api/move", HTTP_GET, handleApiMove);
    server.onNotFound(handleNotFound);
    server.begin();
    webStarted = true;
    Serial.printf("WEB_READY url=http://%s/ and http://%s.local/\n",
                  WiFi.localIP().toString().c_str(), KControllerName);
}

void controllerLoop() {
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'r' || c == 'R') {
            monitorOn = false;
            findEchoServer();
            runSeries();
        } else if (c == 'k' || c == 'K') {
            if (!monitorOn) {                     // calibration needs the monitor
                monitorOn = true;
                findEchoServer();
                window = Window();
            }
            startCalibration();
        } else if (c == 'm' || c == 'M') {
            monitorOn = !monitorOn;
            if (monitorOn) {
                findEchoServer();
                window = Window();
                Serial.printf("MOVE_MONITOR on=1 threshold_db=%.1f\n", KMoveSpreadDb);
                showOnLcd("Move monitor", "starting...");
            } else {
                Serial.println("MOVE_MONITOR on=0");
                showOnLcd("Move monitor off", "m=on r=bandwidth");
            }
        }
    }
    if (monitorOn && sampleWindow()) reportMovement();
    if (webStarted) server.handleClient();
}

// =============================================================================
//  Setup and loop
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(KPinRole, INPUT_PULLUP);
    isEchoServer = digitalRead(KPinRole) == LOW;
    if (isEchoServer) pinMode(KPinPir, INPUT);    // HC-SR501 drives the line itself
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
        startWebServer();
        monitorOn = true;                         // the page shows data right away
        window = Window();
        Serial.printf("MOVE_MONITOR on=1 threshold_db=%.1f\n", KMoveSpreadDb);
    }
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        if (!connectToWifi()) { delay(5000); return; }
        udp.begin(isEchoServer ? KEchoPort : KLocalPort);
    }
    if (isEchoServer) echoLoop();
    else              controllerLoop();
    delay(isEchoServer ? 0 : 5);
}
