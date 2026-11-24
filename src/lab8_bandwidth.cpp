/*
 * =============================================================================
 *  Lab 8 - 8.6 Bandwidth test + movement monitor: one firmware, role by jumper
 * =============================================================================
 *
 *  Version: v8.6.4   (shown in the web page header, the browser tab, and printed
 *                     at boot as "FW_VERSION ...")
 *    Scheme: v8.SECTION.N, tracking the lab outline. The last digit increments
 *            on every change within a section - a fix or small feature (next:
 *            v8.6.5). New functional growth opens the next section and resets
 *            the last digit (e.g. v8.7.0).
 *
 *    v8.6 / 8.6.1: movement monitor (A/B/C vs router, calibration, web page)
 *                  and the 3-leg bandwidth test (r) with terminal output.
 *    v8.6.2:       bandwidth web visualisation - NTP-timestamped runs, a
 *                  per-packet-size chart grid (throughput + RTT, last 20 runs)
 *                  on the web page, and the shaded r-test gap on the movement
 *                  charts.
 *
 *  Role jumper
 *  -----------
 *    GPIO 42 to GND  -> ECHO SERVER  (board B, host name lab8-echo)
 *    GPIO 42 and GPIO 41 to GND -> ECHO SERVER (board C, host name lab8-echo2)
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
 *    Finds each echo server (mDNS lab8-echo.local / lab8-echo2.local, else the
 *    reserved address) and runs the bandwidth series against each (3 legs: A-B,
 *    A-C). Then runs
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
 *    All boards read the router's RSSI every 100 ms (the router sends about
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
 *    /api/move returns the same data as JSON; /api/bw returns the stored
 *    bandwidth runs for a third chart (throughput per packet size, newest run
 *    solid, up to 20 runs). Read-only, local subnet only, script only from the
 *    ESP32 itself (CSP). The monitor starts by itself after the boot series.
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
 *    ROLE role=<echo|controller> host=<lab8-esp32|lab8-echo|lab8-echo2>
 *    INA226 ok=<0|1> addr=<0x4?>
 *    WIFI_STATUS connected=1 hostname=<name> ip=<ip> rssi=<dBm> channel=<n>
 *    echo server:
 *      ECHO_READY port=5001
 *      POWER uptime_s=<s> v=<V> i_ma=<mA> p_mw=<mW> echoed=<n> rejected=<n> pir=<0|1> rssi=<dBm>
 *    controller:
 *      ECHO_TARGET node=<B|C> ip=<ip> via=<mdns|fallback>
 *      ECHO_STATUS when=<before|after> uptime_ms=<ms> v=<V> i_ma=<mA> p_mw=<mW>
 *      BW_RESULT size=<B> sent=<n> lost=<n> changed=<n> rtt_min_us=.. rtt_med_us=..
 *                rtt_avg_us=.. rtt_max_us=.. kbps=.. a_v=.. a_i_ma=.. a_p_mw=..
 *      BW_SERIES start=1 node=<B|C> time=<YYYY-MM-DD HH:MM:SS>  (NTP; stamps the 7 results)
 *      BW_RESULT node=<B|C> size=<B> sent=<n> lost=<n> changed=<n> rtt_min_us=.. ...
 *      BW_SERIES done=1 node=<B|C> echo_restarted=<0|1> rssi=<dBm>
 *      BW_ALL done
 *      CAL state=<leave|recording|done> ... a_base=<dBm> a_calm=<dB> b_base=.. b_calm=..
 *      MOVE uptime_s=<s> a_rssi=<dBm> a_sd=<dB> a_drop=<dB> a_move=<0|1>
 *           b_ok=<0|1> b_rssi=<dBm> b_sd=<dB> b_drop=<dB> b_move=<0|1>
 *           b_pir=<0|1> b_pir_any=<0|1> b_p_mw=<mW>
 *           c_ok=<0|1> c_rssi=<dBm> c_sd=<dB> c_drop=<dB> c_move=<0|1>
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
#include <time.h>

#include "lab8_credentials.h"

// ---------------------------------------------------------------------------
//  Settings
// ---------------------------------------------------------------------------
constexpr uint8_t     KPinRole = 42;              // jumper to GND = echo server
constexpr uint8_t     KPinNode = 41;              // also to GND = second echo server (board C)

constexpr const char* KControllerName = "lab8-esp32";
constexpr const char* KEchoName       = "lab8-echo";
constexpr const char* KEcho2Name      = "lab8-echo2";
// Europe/Stockholm, CET/CEST with automatic daylight saving (POSIX TZ string)
constexpr const char* KTimeZone       = "CET-1CEST,M3.5.0,M10.5.0/3";
const IPAddress       KEchoFallbackIp(192, 168, 32, 51);  // B's reserved address (NFR-NET-1)
const IPAddress       KEcho2FallbackIp(192, 168, 32, 52); // C's reserved address
constexpr uint16_t    KEchoPort  = 5001;
constexpr uint16_t    KLocalPort = 5002;
constexpr size_t      KMaxPacketBytes = 1460;    // Arduino UDP receive buffer limit (1472 is cut to 1460)

constexpr uint16_t KSizes[]        = {32, 64, 128, 256, 512, 1024, 1460};
constexpr int      KSizeCount      = sizeof(KSizes) / sizeof(KSizes[0]);
constexpr int      KPacketsPerSize = 100;
constexpr int      KBwRunsMax      = 20;          // r-runs kept (20 x 7 sizes x 2 legs = 280 values, rotating)
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
IPAddress echoIp;                                 // current target for sendOne()/requestStatus()

// The echo servers the controller measures against (bandwidth test, 3 legs).
struct EchoTarget { const char* name; IPAddress fallback; const char* label; };
const EchoTarget KTargets[] = {
    {KEchoName,  KEchoFallbackIp,  "B"},
    {KEcho2Name, KEcho2FallbackIp, "C"},
};
constexpr int KTargetCount = sizeof(KTargets) / sizeof(KTargets[0]);
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
    int8_t   aRssi, bRssi, cRssi;
    float    aSpread, bSpread, cSpread;
    bool     aMove, bMove, cMove, bOk, cOk, bPirAny;
    uint16_t gapBeforeS = 0;                       // seconds the r test paused just before this sample
};
uint16_t pendingGapS = 0;                          // set by the r handler, consumed by the next sample
MoveSample history[KHistorySize];
int        historyCount = 0;                      // samples stored (max KHistorySize)
int        historyNext  = 0;                      // where the next sample goes

// One bandwidth run for the web chart: throughput (kbit/s) per size, for B and C.
struct BwRun {
    char     when[20];                            // NTP "YYYY-MM-DD HH:MM:SS" or "time-unknown"
    bool     bOk = false, cOk = false;
    uint16_t bKbps[KSizeCount] = {0};             // throughput, kbit/s
    uint16_t cKbps[KSizeCount] = {0};
    uint16_t bRtt[KSizeCount]  = {0};             // median RTT, microseconds
    uint16_t cRtt[KSizeCount]  = {0};
};
BwRun bwRuns[KBwRunsMax];                          // rotating buffer (~140 values)
int   bwRunCount = 0, bwRunNext = 0;
BwRun bwCurrent;                                  // filled while a run is in progress

// Empty-room calibration (controller).
enum CalState { CAL_NONE, CAL_LEAVE, CAL_RECORD, CAL_DONE };
CalState calState = CAL_NONE;
int      calSecondsLeft = 0;
int      calCount = 0, calCountB = 0, calCountC = 0;
int8_t   calA[KCalRecordS], calB[KCalRecordS], calC[KCalRecordS];  // 1 s medians while recording
float    calCalmA = 0, calCalmB = 0, calCalmC = 0;                 // largest spread while recording
float    baseA = 0, baseB = 0, baseC = 0;                          // baseline levels (dBm)

IPAddress monIpB, monIpC;                         // resolved echo servers for the monitor

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

    if (!isEchoServer) {                           // controller: NTP time to stamp each run
        configTzTime(KTimeZone, "pool.ntp.org", "time.nist.gov");
        struct tm t;
        if (getLocalTime(&t, 5000))
            Serial.printf("TIME_SYNC ok=1 time=%04d-%02d-%02d %02d:%02d:%02d\n",
                          t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
        else
            Serial.println("TIME_SYNC ok=0 (no NTP yet; series will print time-unknown)");
    }
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
        Serial.printf("POWER uptime_s=%lu v=%.3f i_ma=%.1f p_mw=%.0f echoed=%lu rejected=%lu pir=%d rssi=%d\n",
                      (unsigned long)(millis() / 1000), p.volts, p.milliamps, p.milliwatts,
                      (unsigned long)echoedPackets, (unsigned long)rejectedPackets,
                      digitalRead(KPinPir), (int)WiFi.RSSI());
    }
}

// =============================================================================
//  Controller role
// =============================================================================

// Resolves one target into echoIp (mDNS by name, else its reserved address).
void resolveEcho(const EchoTarget& t) {
    IPAddress found = MDNS.queryHost(t.name, 2000);
    bool viaMdns = (uint32_t)found != 0;
    echoIp = viaMdns ? found : t.fallback;
    Serial.printf("ECHO_TARGET node=%s ip=%s via=%s\n", t.label,
                  echoIp.toString().c_str(), viaMdns ? "mdns" : "fallback");
}

// The movement monitor queries both echo servers B and C each second.
void resolveMonitorTargets() {
    resolveEcho(KTargets[0]); monIpB = echoIp;
    resolveEcho(KTargets[1]); monIpC = echoIp;
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

// Fills buf with NTP wall-clock time "YYYY-MM-DD HH:MM:SS", or "time-unknown".
void nowStamp(char* buf, size_t n) {
    struct tm t;
    if (getLocalTime(&t, 50)) strftime(buf, n, "%Y-%m-%d %H:%M:%S", &t);
    else                      snprintf(buf, n, "time-unknown");
}

void runSeries(const char* node) {
    char ts[24]; nowStamp(ts, sizeof(ts));        // stamps this leg's 7 results
    Serial.printf("BW_SERIES start=1 node=%s time=%s\n", node, ts);
    char l1[40]; snprintf(l1, sizeof(l1), "Bandwidth %s", node);
    showOnLcd(l1, "running...");

    EchoStatus before = requestStatus();
    printStatus("before", before);
    if (!before.ok) {                             // this server did not answer: skip it
        Serial.printf("BW_SERIES done=0 node=%s reason=NO_ECHO_SERVER\n", node);
        char l2[40]; snprintf(l2, sizeof(l2), "%s no answer", node);
        showOnLcd(l2, "r=retry  k=cal");
        return;
    }

    uint32_t seq = 1;
    for (int i = 0; i < KWarmupPackets; i++) sendOne(seq++, 32);

    bool isNodeB = node[0] == 'B';
    uint16_t* store    = isNodeB ? bwCurrent.bKbps : bwCurrent.cKbps;
    uint16_t* rttStore = isNodeB ? bwCurrent.bRtt  : bwCurrent.cRtt;
    int si = 0;
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
        Serial.printf("BW_RESULT node=%s size=%u sent=%d lost=%d changed=%d rtt_min_us=%lu rtt_med_us=%lu "
                      "rtt_avg_us=%.0f rtt_max_us=%lu kbps=%.0f a_v=%.3f a_i_ma=%.1f a_p_mw=%.0f\n",
                      node, size, KPacketsPerSize, lost, changedPackets, (unsigned long)minUs, (unsigned long)medUs,
                      avgUs, (unsigned long)maxUs, kbps,
                      sum.volts / samples, sum.milliamps / samples, sum.milliwatts / samples);

        if (si < KSizeCount) {                                      // for the web chart
            store[si]    = (uint16_t)(kbps + 0.5f);
            rttStore[si] = medUs > 65535 ? 65535 : (uint16_t)medUs;
        }
        si++;

        char line1[40], line2[40];                   // showOnLcd() cuts to 16
        snprintf(line1, sizeof(line1), "%s %uB %.1fms", node, size, medUs / 1000.0f);
        snprintf(line2, sizeof(line2), "L%d %.0fkbps", lost, kbps);
        showOnLcd(line1, line2);
    }
    if (isNodeB) bwCurrent.bOk = true; else bwCurrent.cOk = true;   // this leg completed

    EchoStatus after = requestStatus();
    printStatus("after", after);
    bool restarted = before.ok && after.ok && after.uptimeMs < before.uptimeMs;   // FR-BW-6
    Serial.printf("BW_SERIES done=1 node=%s echo_restarted=%d rssi=%d\n",
                  node, restarted ? 1 : 0, (int)WiFi.RSSI());
}

// Runs the bandwidth series against every echo server in turn (3 legs: A-B, A-C).
void runAllSeries() {
    bwCurrent = BwRun();                              // clear; one timestamp for the whole run
    nowStamp(bwCurrent.when, sizeof(bwCurrent.when));
    for (int i = 0; i < KTargetCount; i++) {
        resolveEcho(KTargets[i]);
        runSeries(KTargets[i].label);
    }
    bwRuns[bwRunNext] = bwCurrent;                    // commit to the rotating buffer
    bwRunNext = (bwRunNext + 1) % KBwRunsMax;
    if (bwRunCount < KBwRunsMax) bwRunCount++;
    Serial.println("BW_ALL done (monitor resumes; send r to run again, k to calibrate)");
    showOnLcd("All legs done", "r=again  k=cal");
}

// =============================================================================
//  Empty-room calibration (FR-RADAR-4)
// =============================================================================
void startCalibration() {
    calState = CAL_LEAVE;
    calSecondsLeft = KCalLeaveS;
    calCount = calCountB = calCountC = 0;
    calCalmA = calCalmB = calCalmC = 0;
    Serial.printf("CAL state=leave seconds=%d (nobody within 2 m of a board, nobody on the paths)\n", KCalLeaveS);
}

int8_t medianOf(int8_t* v, int n) {
    std::sort(v, v + n);
    return v[n / 2];
}

// Called once per second with the newest windows of both links.
void stepCalibration(int aRssi, float aSd, bool bOk, int bRssi, float bSd,
                     bool cOk, int cRssi, float cSd) {
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
    if (cOk) {
        calC[calCountC++] = cRssi;
        if (cSd > calCalmC) calCalmC = cSd;
    }
    if (--calSecondsLeft > 0) return;

    baseA = medianOf(calA, calCount);
    baseB = calCountB > 0 ? medianOf(calB, calCountB) : 0;
    baseC = calCountC > 0 ? medianOf(calC, calCountC) : 0;
    calState = CAL_DONE;
    Serial.printf("CAL state=done a_base=%.0f a_calm=%.2f b_base=%.0f b_calm=%.2f b_seconds=%d "
                  "rule: drop>=%.1f dB or spread>calm+%.1f dB\n",
                  baseA, calCalmA, baseB, calCalmB, calCountB, KCalDropDb, KCalSpreadMarginDb);
    if (calCountB < KCalRecordS / 2) Serial.println("CAL warning: B answered less than half the time, run k again");
}

// Asks B for its last window and prints one MOVE line with both links.
// One echo server's reply to MEAS?: its RSSI window, PIR and power.
struct Meas {
    bool ok = false;
    int  rssi = 0, pir = 0, pirAny = 0, edges = 0;
    float sd = 0, p = 0;
};

Meas queryMeas(IPAddress ip) {
    Meas m;
    drainUdp();
    udp.beginPacket(ip, KEchoPort);
    udp.write((const uint8_t*)"MEAS?", 5);
    udp.endPacket();
    int64_t start = esp_timer_get_time();
    while (esp_timer_get_time() - start < KMeasTimeoutUs) {
        if (udp.parsePacket() <= 0) continue;
        int n = udp.read(rxBuffer, sizeof(rxBuffer) - 1);
        if (n < 0) n = 0;
        rxBuffer[n] = 0;
        int bN = 0; float v = 0, i = 0;
        if (sscanf((char*)rxBuffer,
                   "MEAS rssi=%d sd=%f n=%d pir=%d pir_any=%d pir_edges=%d v=%f i_ma=%f p_mw=%f",
                   &m.rssi, &m.sd, &bN, &m.pir, &m.pirAny, &m.edges, &v, &i, &m.p) == 9) {
            m.ok = bN > 0;
            break;
        }
    }
    return m;
}

void reportMovement() {
    Meas b = queryMeas(monIpB);
    Meas c = queryMeas(monIpC);
    int   aRssi = lastWindow.median;
    float aSd   = lastWindow.spread;

    stepCalibration(aRssi, aSd, b.ok, b.rssi, b.sd, c.ok, c.rssi, c.sd);

    float aDrop = 0, bDrop = 0, cDrop = 0;
    bool aMove, bMove, cMove;
    if (calState == CAL_DONE) {
        aDrop = baseA - aRssi;                    // positive = weaker than the empty room
        bDrop = baseB - b.rssi;
        cDrop = baseC - c.rssi;
        aMove = aDrop >= KCalDropDb || aSd > calCalmA + KCalSpreadMarginDb;
        bMove = b.ok && (bDrop >= KCalDropDb || b.sd > calCalmB + KCalSpreadMarginDb);
        cMove = c.ok && (cDrop >= KCalDropDb || c.sd > calCalmC + KCalSpreadMarginDb);
    } else {
        aMove = aSd > KMoveSpreadDb;
        bMove = b.ok && b.sd > KMoveSpreadDb;
        cMove = c.ok && c.sd > KMoveSpreadDb;
    }
    Serial.printf("MOVE uptime_s=%lu a_rssi=%d a_sd=%.2f a_drop=%.1f a_move=%d "
                  "b_ok=%d b_rssi=%d b_sd=%.2f b_drop=%.1f b_move=%d b_pir=%d b_pir_any=%d b_p_mw=%.0f "
                  "c_ok=%d c_rssi=%d c_sd=%.2f c_drop=%.1f c_move=%d\n",
                  (unsigned long)(millis() / 1000), aRssi, aSd, aDrop, aMove ? 1 : 0,
                  b.ok ? 1 : 0, b.rssi, b.sd, bDrop, bMove ? 1 : 0, b.pir, b.pirAny, b.p,
                  c.ok ? 1 : 0, c.rssi, c.sd, cDrop, cMove ? 1 : 0);

    MoveSample& h = history[historyNext];
    h.uptimeS = millis() / 1000;
    h.aRssi = aRssi;    h.aSpread = aSd;    h.aMove = aMove;
    h.bRssi = b.rssi;   h.bSpread = b.sd;   h.bMove = bMove;  h.bOk = b.ok;  h.bPirAny = b.ok && b.pirAny;
    h.cRssi = c.rssi;   h.cSpread = c.sd;   h.cMove = cMove;  h.cOk = c.ok;
    h.gapBeforeS = pendingGapS; pendingGapS = 0;   // first sample after an r run carries the pause length
    historyNext = (historyNext + 1) % KHistorySize;
    if (historyCount < KHistorySize) historyCount++;

    char line1[40], line2[40];                   // showOnLcd() cuts to 16
    snprintf(line1, sizeof(line1), "A%d B%d C%d", aRssi, b.ok ? b.rssi : 0, c.ok ? c.rssi : 0);
    snprintf(line2, sizeof(line2), "move %s%s%s", aMove ? "A" : "-", bMove ? "B" : "-", cMove ? "C" : "-");
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
<title>Lab 8 movement v8.6.4</title>
<style>
:root{--bg:#f6f5f2;--card:#fff;--ink:#1d1d1b;--mute:#6b6a66;--line:#e2e0da;
--a:#2563eb;--b:#d97706;--c:#059669;--pir:rgba(120,120,120,.16);--move:#dc2626}
@media (prefers-color-scheme:dark){:root{--bg:#141413;--card:#1f1f1d;--ink:#eceae4;
--mute:#9c9a93;--line:#34332f;--a:#60a5fa;--b:#fbbf24;--c:#34d399;--pir:rgba(200,200,200,.12);--move:#f87171}}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.4 system-ui,sans-serif}
main{max-width:960px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:0 0 4px}p{margin:0 0 12px;color:var(--mute)}
.boxes{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:8px;margin-bottom:12px}
.box{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px}
.box b{display:block;font-size:13px;color:var(--mute);font-weight:500}
.box span{font-size:20px;font-weight:600}
.on{color:var(--move)}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px;margin-bottom:12px}
.card h2{font-size:14px;margin:0 0 6px;font-weight:600}
canvas{width:100%;height:200px;display:block}
.bwgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:8px}
.bwcell h3{font-size:12px;color:var(--mute);margin:0 0 2px;font-weight:600}
.bwcell canvas{height:110px}
.key{font-size:13px;color:var(--mute)}.key i{display:inline-block;width:12px;height:3px;margin:0 4px 3px 10px;vertical-align:middle}
h1 .ver{font-size:14px;color:var(--mute);font-weight:400;margin-left:8px}
</style></head><body><main>
<h1>Lab 8 &ndash; movement monitor<span class="ver">v8.6.4</span></h1>
<p id="info">Waiting for data&hellip;</p>
<div class="boxes">
<div class="box"><b>Link A &ndash; router</b><span id="ma">&ndash;</span></div>
<div class="box"><b>Link B &ndash; router</b><span id="mb">&ndash;</span></div>
<div class="box"><b>Link C &ndash; router</b><span id="mc">&ndash;</span></div>
<div class="box"><b>PIR at B</b><span id="mp">&ndash;</span></div>
</div>
<div class="card"><h2>Signal strength (dBm, median per second)</h2><canvas id="c1"></canvas>
<div class="key"><i style="background:var(--a)"></i>A&ndash;router<i style="background:var(--b)"></i>B&ndash;router<i style="background:var(--c)"></i>C&ndash;router<i style="background:var(--pir);height:10px"></i>PIR at B saw movement &middot; dashed = empty-room baseline</div></div>
<div class="card"><h2>Spread (dB, standard deviation per second)</h2><canvas id="c2"></canvas>
<div class="key"><i style="background:var(--a)"></i>A<i style="background:var(--b)"></i>B<i style="background:var(--c)"></i>C &middot; dashed = movement threshold</div></div>
<div class="card"><h2>Bandwidth test (r) &ndash; throughput over runs, one graph per packet size</h2>
<div id="bwgrid" class="bwgrid"></div>
<div class="key"><i style="background:var(--b)"></i>B<i style="background:var(--c)"></i>C &middot; solid = throughput (Mbit/s, left), dashed = median RTT (ms, right) &middot; newest run at the right &middot; <span id="bwinfo">no run yet</span></div></div>
</main><script src="/app.js"></script></body></html>
)HTML";

const char KPageJs[] PROGMEM = R"JS(
const css=n=>getComputedStyle(document.documentElement).getPropertyValue(n).trim();
function draw(id,rows,series,lo,hi,thr,refs){
  const c=document.getElementById(id),r=window.devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;
  c.width=w*r;c.height=h*r;const g=c.getContext('2d');g.scale(r,r);g.clearRect(0,0,w,h);
  const L=36,R=w-6,T=6,B=h-18,W=180,GAP=9,y=v=>B-(B-T)*(v-lo)/(hi-lo);   // W=seconds shown, GAP=pause drawn at ~5%
  // effective time axis: a pause is compressed to GAP s so it reads as a narrow band, not real width
  const te=[];let acc=0;for(let i=0;i<rows.length;i++){if(i>0){let d=rows[i][0]-rows[i-1][0];if(d<0)d=0;if(rows[i][13]>0)d=rows[i][13]*0.67;else if(d>3)d=GAP;acc+=d}te.push(acc)}
  const tNow=te.length?te[te.length-1]:0;
  const x=t=>{const xx=R-(R-L)*(tNow-t)/W;return xx<L?L:xx},cw=(R-L)/W;
  g.fillStyle=css('--pir');rows.forEach((s,i)=>{if(s[8]&&te[i]>=tNow-W)g.fillRect(x(te[i])-cw/2,T,cw+1,B-T)});
  for(let i=1;i<rows.length;i++){if(rows[i][0]-rows[i-1][0]>3&&te[i]>=tNow-W){   // shade the r-test pause (~5% wide)
    const x0=x(te[i-1]),x1=x(te[i]);g.fillStyle='rgba(130,130,145,0.22)';g.fillRect(x0,T,x1-x0,B-T);
    if(x1-x0>24){g.fillStyle=css('--mute');g.textAlign='center';g.font='9px system-ui';g.fillText('r',(x0+x1)/2,T+11);g.textAlign='left'}}}
  g.strokeStyle=css('--line');g.fillStyle=css('--mute');g.font='11px system-ui';g.lineWidth=1;
  for(let k=0;k<=4;k++){const v=lo+(hi-lo)*k/4,yy=y(v);g.beginPath();g.moveTo(L,yy);g.lineTo(R,yy);g.stroke();g.fillText(v.toFixed(hi-lo<10?1:0),2,yy+4)}
  g.fillText('-3 min',L,h-4);g.fillText('now',R-22,h-4);
  if(thr!=null){g.strokeStyle=css('--move');g.setLineDash([4,4]);g.beginPath();g.moveTo(L,y(thr));g.lineTo(R,y(thr));g.stroke();g.setLineDash([])}
  (refs||[]).forEach(([v,col])=>{g.strokeStyle=css(col);g.lineWidth=1;g.setLineDash([5,4]);g.beginPath();g.moveTo(L,y(v));g.lineTo(R,y(v));g.stroke();g.setLineDash([])});
  series.forEach(([k,col,okc])=>{g.strokeStyle=css(col);g.lineWidth=2;g.beginPath();let up=false;
    rows.forEach((s,i)=>{if(te[i]<tNow-W){up=false;return}if(okc>=0&&!s[okc]){up=false;return}if(i>0&&s[0]-rows[i-1][0]>3){up=false}const v=Math.max(lo,Math.min(hi,s[k]));
      if(up)g.lineTo(x(te[i]),y(v));else{g.moveTo(x(te[i]),y(v));up=true}});g.stroke()});
}
function setBox(id,on,text){const e=document.getElementById(id);e.textContent=text;e.className=on?'on':''}
// One mini-chart per packet size: throughput (Mbit/s, solid, left axis) and
// median RTT (ms, dashed, right axis) over runs; B amber, C emerald.
function drawMini(canvas,tput,rtt,n,whens){
  const r=window.devicePixelRatio||1,w=canvas.clientWidth,h=canvas.clientHeight;
  canvas.width=w*r;canvas.height=h*r;const g=canvas.getContext('2d');g.scale(r,r);g.clearRect(0,0,w,h);
  const L=28,R=w-22,T=13,B=h-13,x=i=>L+(R-L)*(n<2?0.5:i/(n-1));
  let hiT=0.05;tput.forEach(s=>s.vals.forEach(v=>{if(v!=null)hiT=Math.max(hiT,v/1000)}));hiT=Math.ceil(hiT*20)/20;
  let hiR=1;rtt.forEach(s=>s.vals.forEach(v=>{if(v!=null)hiR=Math.max(hiR,v/1000)}));hiR=Math.ceil(hiR);
  const yT=v=>B-(B-T)*v/hiT,yR=v=>B-(B-T)*v/hiR;
  g.font='9px system-ui';g.lineWidth=1;g.fillStyle=css('--mute');
  g.textAlign='left';g.fillText('Mbit/s',0,8);g.textAlign='right';g.fillText('ms',w,8);
  for(let k=0;k<=2;k++){const yy=T+(B-T)*(1-k/2);
    g.strokeStyle=css('--line');g.beginPath();g.moveTo(L,yy);g.lineTo(R,yy);g.stroke();
    g.fillStyle=css('--mute');g.textAlign='left';g.fillText((hiT*k/2).toFixed(2),0,yy+3);
    g.textAlign='right';g.fillText((hiR*k/2).toFixed(0),w,yy+3)}
  g.textAlign='left';
  const dots=n<=40;                               // dots only when sparse enough to read
  tput.forEach(s=>{g.strokeStyle=css(s.col);g.fillStyle=css(s.col);g.lineWidth=2;let up=false;g.beginPath();
    s.vals.forEach((v,i)=>{if(v==null){up=false;return}const yy=yT(v/1000);if(up)g.lineTo(x(i),yy);else{g.moveTo(x(i),yy);up=true}});g.stroke();
    if(dots)s.vals.forEach((v,i)=>{if(v==null)return;g.beginPath();g.arc(x(i),yT(v/1000),2,0,7);g.fill()})});
  g.setLineDash([3,3]);
  rtt.forEach(s=>{g.strokeStyle=css(s.col);g.lineWidth=1.5;let up=false;g.beginPath();
    s.vals.forEach((v,i)=>{if(v==null){up=false;return}const yy=yR(v/1000);if(up)g.lineTo(x(i),yy);else{g.moveTo(x(i),yy);up=true}});g.stroke()});
  g.setLineDash([]);
  if(whens&&whens.length){const hm=s=>s&&s.length>=16?s.substr(11,5):'';g.fillStyle=css('--mute');
    g.textAlign='left';g.fillText(hm(whens[0]),L-2,h-3);
    if(n>1){g.textAlign='right';g.fillText(hm(whens[whens.length-1]),R,h-3)}g.textAlign='left'}
}
// Builds the 7 cells once, then fills each from the per-size run history.
function drawBwGrid(sizes,runs){
  const grid=document.getElementById('bwgrid');
  if(grid.childElementCount!==sizes.length){grid.innerHTML='';
    sizes.forEach((s,i)=>{const d=document.createElement('div');d.className='bwcell';
      d.innerHTML='<h3 id="bwh'+i+'">'+s+' B</h3><canvas id="bw'+i+'"></canvas>';grid.appendChild(d)})}
  const n=runs.length,whens=runs.map(u=>u.when);
  const avg=a=>{const v=a.filter(x=>x!=null);return v.length?v.reduce((p,q)=>p+q,0)/v.length:null};
  const leg=(t,r,col,lbl)=>t==null?'':'<br><span style="color:var('+col+')">'+lbl+':</span> '+(t/1000).toFixed(3)+' Mbit/s · '+(r/1000).toFixed(2)+' ms';
  sizes.forEach((s,i)=>{
    const bt=runs.map(u=>u.b?u.b[i]:null),ct=runs.map(u=>u.c?u.c[i]:null);
    const br=runs.map(u=>u.brtt?u.brtt[i]:null),cr=runs.map(u=>u.crtt?u.crtt[i]:null);
    document.getElementById('bwh'+i).innerHTML=s+' B'+leg(avg(bt),avg(br),'--b','B')+leg(avg(ct),avg(cr),'--c','C');
    drawMini(document.getElementById('bw'+i),
             [{vals:bt,col:'--b'},{vals:ct,col:'--c'}],
             [{vals:br,col:'--b'},{vals:cr,col:'--c'}],n,whens);
  });
}
async function bwTick(){
  try{
    const d=await (await fetch('/api/bw',{cache:'no-store'})).json();
    drawBwGrid(d.sizes,d.runs);
    const el=document.getElementById('bwinfo');
    if(d.runs.length){const last=d.runs[d.runs.length-1];el.textContent=d.runs.length+' run(s), latest '+last.when}
    else el.textContent='no run yet';
  }catch(e){}
}
async function tick(){
  try{
    const d=await (await fetch('/api/move',{cache:'no-store'})).json(),rows=d.rows;
    if(!rows.length){document.getElementById('info').textContent='Monitor running, waiting for the first second';return}
    const s=rows[rows.length-1];
    const cal=d.cal==='done'?'calibrated: movement = level '+d.drop+' dB below the dashed baseline, or spread above the dashed line'
      :d.cal==='leave'?'CALIBRATION: leave the area, recording starts in '+d.calLeft+' s'
      :d.cal==='recording'?'CALIBRATION: recording the empty room, keep away '+d.calLeft+' s'
      :'not calibrated (send k): movement = spread above '+d.threshold+' dB';
    document.getElementById('info').textContent='Uptime '+s[0]+' s \u00b7 '+cal;
    setBox('ma',s[3],s[3]?'MOVEMENT':'quiet');
    setBox('mb',s[7],!s[4]?'no answer':(s[7]?'MOVEMENT':'quiet'));
    setBox('mc',s[12],!s[9]?'no answer':(s[12]?'MOVEMENT':'quiet'));
    setBox('mp',s[8],!s[4]?'–':(s[8]?'movement':'quiet'));
    const done=d.cal==='done';
    let lo=0,hi=-100;rows.forEach(r=>{lo=Math.min(lo,r[1],r[4]?r[5]:0,r[9]?r[10]:0);hi=Math.max(hi,r[1],r[4]?r[5]:-100,r[9]?r[10]:-100)});
    if(done){lo=Math.min(lo,d.baseA-d.drop,d.baseB-d.drop,d.baseC-d.drop);hi=Math.max(hi,d.baseA,d.baseB,d.baseC)}
    const rssiSeries=[[1,'--a',-1],[5,'--b',4],[10,'--c',9]];
    draw('c1',rows,rssiSeries,Math.floor(lo/5)*5-5,Math.ceil(hi/5)*5+5,null,done?[[d.baseA,'--a'],[d.baseB,'--b'],[d.baseC,'--c']]:[]);
    let sm=d.threshold+1;rows.forEach(r=>{sm=Math.max(sm,r[2],r[6],r[11])});if(done)sm=Math.max(sm,d.thrA,d.thrB,d.thrC);
    const sdSeries=[[2,'--a',-1],[6,'--b',4],[11,'--c',9]];
    draw('c2',rows,sdSeries,0,Math.ceil(sm),done?null:d.threshold,done?[[d.thrA,'--a'],[d.thrB,'--b'],[d.thrC,'--c']]:[]);
  }catch(e){document.getElementById('info').textContent='No contact with the ESP32'}
}
tick();setInterval(tick,1000);
bwTick();setInterval(bwTick,5000);                 // bandwidth changes only on r
addEventListener('resize',()=>{tick();bwTick()});
)JS";

void sendSecurityHeaders() {
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("X-Content-Type-Options", "nosniff");
    server.sendHeader("X-Frame-Options", "DENY");
    server.sendHeader("Referrer-Policy", "no-referrer");
    // Script only from this ESP32 (/app.js), data only from /api/move and /api/bw.
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

void handlePage() { if (acceptRequest()) { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "text/html; charset=utf-8", KPageHtml); } }
void handleJs()   { if (acceptRequest()) { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "text/javascript; charset=utf-8", KPageJs); } }

// rows: [uptime_s, a_rssi, a_sd, a_move, b_ok, b_rssi, b_sd, b_move, b_pir_any,
//        c_ok, c_rssi, c_sd, c_move], oldest first.
void handleApiMove() {
    if (!acceptRequest()) return;
    String json;
    json.reserve(240 + historyCount * 72);
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
        json += ",\"baseC\":";  json += String(baseC, 0);
        json += ",\"thrA\":";   json += String(calCalmA + KCalSpreadMarginDb, 2);
        json += ",\"thrB\":";   json += String(calCalmB + KCalSpreadMarginDb, 2);
        json += ",\"thrC\":";   json += String(calCalmC + KCalSpreadMarginDb, 2);
        json += ",\"drop\":";   json += String(KCalDropDb, 1);
    }
    json += ",\"rows\":[";
    int first = (historyNext - historyCount + KHistorySize) % KHistorySize;
    char item[104];
    for (int i = 0; i < historyCount; i++) {
        const MoveSample& h = history[(first + i) % KHistorySize];
        snprintf(item, sizeof(item), "%s[%lu,%d,%.2f,%d,%d,%d,%.2f,%d,%d,%d,%d,%.2f,%d,%u]", i ? "," : "",
                 (unsigned long)h.uptimeS, h.aRssi, h.aSpread, h.aMove, h.bOk,
                 h.bRssi, h.bSpread, h.bMove, h.bPirAny,
                 h.cOk, h.cRssi, h.cSpread, h.cMove, h.gapBeforeS);
        json += item;
    }
    json += "]}";
    server.send(200, "application/json", json);
}

// Bandwidth runs for the third chart: { sizes:[..], runs:[{when, b:[kbps..], c:[..]}..] }.
void appendKbps(String& json, const uint16_t* a) {
    json += "[";
    for (int i = 0; i < KSizeCount; i++) { if (i) json += ","; json += String(a[i]); }
    json += "]";
}
void handleApiBw() {
    if (!acceptRequest()) return;
    String json;
    json.reserve(96 + bwRunCount * 320);
    json += "{\"sizes\":[";
    for (int i = 0; i < KSizeCount; i++) { if (i) json += ","; json += String(KSizes[i]); }
    json += "],\"runs\":[";
    int first = (bwRunNext - bwRunCount + KBwRunsMax) % KBwRunsMax;
    for (int r = 0; r < bwRunCount; r++) {
        const BwRun& b = bwRuns[(first + r) % KBwRunsMax];
        if (r) json += ",";
        json += "{\"when\":\""; json += b.when; json += "\",\"b\":";
        if (b.bOk) appendKbps(json, b.bKbps); else json += "null";
        json += ",\"c\":";
        if (b.cOk) appendKbps(json, b.cKbps); else json += "null";
        json += ",\"brtt\":";
        if (b.bOk) appendKbps(json, b.bRtt); else json += "null";
        json += ",\"crtt\":";
        if (b.cOk) appendKbps(json, b.cRtt); else json += "null";
        json += "}";
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
    server.on("/api/bw", HTTP_GET, handleApiBw);
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
            uint32_t runStartMs = millis();
            runAllSeries();
            pendingGapS = (uint16_t)((millis() - runStartMs + 500) / 1000);   // real r duration (~5-7 s)
            monitorOn = true;                     // auto-continue with the movement monitor
            resolveMonitorTargets();
            window = Window();
            Serial.printf("MOVE_MONITOR on=1 threshold_db=%.1f\n", KMoveSpreadDb);
        } else if (c == 'k' || c == 'K') {
            if (!monitorOn) {                     // calibration needs the monitor
                monitorOn = true;
                resolveMonitorTargets();
                window = Window();
            }
            startCalibration();
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
    pinMode(KPinNode, INPUT_PULLUP);
    bool isBoardC = isEchoServer && digitalRead(KPinNode) == LOW;
    // HC-SR501 drives the line itself; the pull-down keeps an unconnected pin at 0.
    if (isEchoServer) pinMode(KPinPir, INPUT_PULLDOWN);
    const char* hostName = !isEchoServer ? KControllerName : (isBoardC ? KEcho2Name : KEchoName);
    Serial.println("FW_VERSION v8.6.4");
    Serial.printf("ROLE role=%s host=%s\n", isEchoServer ? "echo" : "controller", hostName);

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
        runAllSeries();
        resolveMonitorTargets();                  // monitor uses B and C
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
