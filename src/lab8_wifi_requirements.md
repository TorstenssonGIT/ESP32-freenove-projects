---
title: "Lab 8 – Wi-Fi on the ESP32-S3: Requirements specification"
author: "Johan Torstensson"
date: "2026-11-21"
---

# 1. Purpose and scope

This document specifies the requirements for Lab 8: what the system shall do
(functional requirements), how well it shall do it (non-functional
requirements), which security rules apply, and how each requirement is
verified. Each requirement has an ID, a priority and a status, and is traced
to a lab step and a test.

Related documents: the plan `lab8_wifi.md`, the real-time design
`lab8_wifi_rt_design.md`, the security analysis
`lab8_wifi_security_analysis.md`, the PR log `lab8_wifi_pr.md` and the
diagrams `lab8_*.svg`.

**Priority:** M = must, S = should, C = could.
**Status:** Verified = tested on the board; Implemented = code written, not
yet tested; Planned = not yet implemented.
**Verification:** T = automated smoke-test, M = manual test on the board,
I = inspection (code, files or documentation).

# 2. Definitions

| Term | Meaning |
|----|--------------|
| Controller | Board A: Freenove ESP32-S3 (N8R8, 8 MB flash), MAC `E8:F6:0A:A8:05:98`, host name `lab8-esp32`, with LCD and LED. Runs the Lab 8 programs and the bandwidth test. |
| Echo server | Board B: Freenove ESP32-S3 (N16R8, 16 MB flash), MAC `44:B1:76:A2:FC:2C`, host name `lab8-echo`. Bandwidth Test Echo Server. |
| Board C | Freenove ESP32-S3 (from the 2-pack), host name `lab8-echo2`; second radar slave (phase 2), planned. |
| Router | ZTE MC888 5G internet router; 2.4 GHz network, WPA2, `192.168.32.0/24`. |
| Printer | HP OfficeJet 8010, reached through its own Wi-Fi Direct network. |
| NVS | Non-volatile storage in the ESP32's flash, used for credentials. |
| Router mode / direct mode | 8.6: traffic via the router, or the echo server as its own access point with a direct radio path. |

# 3. Hardware requirements

| ID | Item | Description | Requirement | Pri | Verif. | Status |
|-----|------|---------|------------------------------|--|---|--------|
| HW-1 | LCD 16×2 with PCF8574T, LED | Character display with I2C backpack; yellow status LED | The controller shall use the existing LCD 16x2 with PCF8574T on I2C (SDA GPIO 1, SCL GPIO 2, address 0x27) and the yellow LED on GPIO 13 via a series resistor. | M | I | Verified |
| HW-2 | ESP32-S3 (board B) | Freenove ESP32-S3 WROOM N16R8 development board, Wi-Fi 2.4 GHz | The echo server shall run on USB power alone (power bank or cable) after its first setup. | M | M | Verified (runs on its own USB cable with INA226) |
| HW-3 | INA226 | Current, voltage and power monitor board (I2C), 100 mΩ shunt | The controller (A) and the echo server (B) shall each have an INA226 power monitor (I2C, 100 mΩ shunt, VCC 3.3 V) in series with the 5 V wire of their USB cable (only VBUS cut through a window; GND, data wires and shield untouched), with VBS on IN−, on the bus at GPIO 1/2 (address 0x44). On A the bus is shared with the LCD (0x44 is taken by the Modulino on the sensor bus); B has no LCD and needs its own 10 kΩ pull-ups from GPIO 1/2 to 3V3. | S | M | Verified: A 5.27–5.29 V, 84–90 mA, ≈ 445–470 mW at rest; B 5.30 V, 41.6 mA, 221 mW at rest |
| HW-4 | Modulino Thermo (HS3003) | Arduino temperature and humidity sensor board (I2C, Qwiic) | The 3.3 V sensors shall have their own I2C bus (`Wire1`: SDA GPIO 47, SCL GPIO 21, 100 kHz, 10 kΩ pull-ups to 3V3), separate from the 5 V LCD bus, chained over Qwiic cables with a total length under 1 m (10 + 10 + 40 cm). The Arduino Modulino Thermo (HS3003, 0x44) shall sit inside the box and log the box climate and antenna conditions. | S | M | Verified (Modulino) |
| HW-8 | DHT11 | Temperature and humidity sensor module (one-wire) | The kit's DHT11 (DATA GPIO 14, 3.3 V, 10 kΩ pull-up on the module) shall sit above the box as the room reference, to verify that the SHT31-D has recovered after each steam condition: the offset SHT31-D − DHT11 after drying shall be within ±2 % RH of the offset before. | S | M | Verified (wiring, readings); check Planned |
| HW-9 | SHT31-D | Temperature and humidity sensor board with built-in heater (I2C, Qwiic) | An SHT31-D with heater (address 0x45 by its address jumper, last in the Qwiic chain on a 40 cm cable) shall measure the humidity above the steam. | S | M | Verified (reading; heater not yet used) |
| HW-10 | TMAG5273 | 3D Hall-effect magnetic sensor board (I2C, Qwiic), with Ø5×3 mm magnet | A TMAG5273 3D Hall sensor (0x35) on the same bus shall detect the reflector's index magnet. | C | M | Verified (background 0.04 mT, magnet up to 80 mT per axis) |
| HW-11 | HC-SR501 PIR | Passive infrared (PIR) motion sensor | Board B shall have an HC-SR501 PIR sensor (VCC 5 V, OUT GPIO 14, jumper on H, shortest hold time) as ground truth for movement; readings shall be ignored for 60 s after power-on. | C | M | Wired (VCC, OUT GPIO 14, GND; jumper on H); test Planned |
| HW-5 | ESP32-S3 (all boards) | Freenove ESP32-S3 WROOM development boards (A: N8R8, B: N16R8, C) | GPIO 19/20 (native USB), the boot pins (0, 3, 45, 46) and the pins used by flash and PSRAM shall not be used for wiring. | M | I | Verified |
| HW-6 | USB power cable | USB cable that powers the measured board | During power measurement the measured cable shall be the board's only power source. | M | M | Planned |
| HW-7 | 28BYJ-48 + ULN2003, reflector | 5 V stepper motor with Darlington driver board; foil-and-paper half-cylinder | The radar shall use a 28BYJ-48 stepper motor with ULN2003 driver (IN1–IN4 on GPIO 4–7, camera unplugged; 5 V from its own USB-C power cable, not through the INA226; shared GND) turning a cylindrical reflector (Ø 6.2 cm, foil on 180°, ≥ 6 cm high) around the antenna; see `lab8_radar_reflector.md`. | C | M | Motor Verified (4076 half steps, 12.2 s per turn; own supply: dip 0.03 V); reflector Planned |

# 4. Functional requirements

## 4.1 Scan (8.1–8.3)

| ID | Requirement | Pri | Verif. | Status |
|-----|------------------------------|--|---|--------|
| FR-SCAN-1 | The controller shall scan nearby 2.4 GHz networks every 15 s without joining any of them. | M | T | Verified |
| FR-SCAN-2 | Serial shall report each network with index, SSID as hex and as text, RSSI, channel and security mode (`SCAN_BEGIN` … `SCAN_END`). | M | T | Verified |
| FR-SCAN-3 | The LCD shall show the number of networks and then each SSID (16 characters), RSSI and channel. | M | M | Verified |
| FR-SCAN-4 | Open networks shall be clearly identified. | M | T | Verified |

## 4.2 Connection and internet check (8.4)

| ID | Requirement | Pri | Verif. | Status |
|-----|------------------------------|--|---|--------|
| FR-CONN-1 | The controller shall join the configured 2.4 GHz network as host name `lab8-esp32`. | M | T | Verified |
| FR-CONN-2 | Serial shall report IP, gateway, subnet, DNS, RSSI and channel; the LCD shall show host name and IP. | M | T | Verified |
| FR-CONN-3 | Internet access shall be checked every 30 s against `http://connectivitycheck.gstatic.com/generate_204`; HTTP 204 = OK. | M | T | Verified |
| FR-CONN-4 | A lost connection shall be detected and re-established automatically; the LCD shall show the failure reason meanwhile. | M | M | Verified |

## 4.3 Credentials (8.4b)

| ID | Requirement | Pri | Verif. | Status |
|-----|------------------------------|--|---|--------|
| FR-CRED-1 | Credentials shall be entered over Serial in a setup menu that opens automatically when none are stored, or when `c` is pressed within 3 s after reset. | M | M | Verified |
| FR-CRED-2 | The menu shall store the router network and the printer's Wi-Fi Direct network (SSID, password, IP, port), show stored values and erase them. | M | M | Verified (erase not yet tested) |
| FR-CRED-3 | Input shall be validated: SSID 1–32 characters, password 8–63, valid IPv4 address, port 1–65535. | M | I | Implemented |
| FR-CRED-4 | Credentials shall survive power-off, reset and new uploads. | M | M | Verified |

## 4.4 Web status page (8.5, 8.5b)

| ID | Requirement | Pri | Verif. | Status |
|-----|------------------------------|--|---|--------|
| FR-WEB-1 | The controller shall serve a status page at `http://<ip>/` with connection, internet and scan data, refreshing every 15 s. | M | T | Verified |
| FR-WEB-2 | `GET /api/status` shall return the same data as JSON. | M | T | Verified |
| FR-WEB-3 | Nearby networks shall be scanned every 60 s in the background without disconnecting. | M | T | Verified |
| FR-WEB-4 | Connect time, connect attempts, reconnections and uptime shall be measured and shown. | M | T | Verified |
| FR-WEB-5 | The clock shall be set by NTP with Swedish time (CET/CEST) and shown on the page and in JSON. | M | T | Verified |
| FR-WEB-6 | The page shall draw graphs (inline SVG, no JavaScript) of RSSI and internet response time for the last hour (ring buffer). | S | T | Planned (8.5b) |
| FR-WEB-7 | `GET /api/history` shall return the graph data as JSON. | S | T | Planned (8.5b) |

## 4.5 Bandwidth and latency test (8.6)

| ID | Requirement | Pri | Verif. | Status |
|-----|------------------------------|--|---|--------|
| FR-BW-1 | The echo server shall return every UDP packet received on port 5001 unchanged. | M | T | Planned |
| FR-BW-2 | The controller shall send 100 packets per size for the sizes 32, 64, 128, 256, 512, 1024 and 1472 bytes, one at a time. | M | T | Planned |
| FR-BW-3 | A packet not returned within 500 ms shall count as lost; returned packets shall be checked for sequence number and content. | M | T | Planned |
| FR-BW-4 | Per size, the controller shall report min, median, average and max round-trip time (µs), losses and throughput (2 × size / average RTT) on Serial (`BW_RESULT`), the LCD and the web page. | M | T | Planned |
| FR-BW-5 | The test shall run in router mode or direct mode (echo server as access point), chosen in the menu. | M | M | Planned |
| FR-BW-6 | The controller shall read the echo server's uptime before and after each series over UDP and mark the series unreliable if the echo server restarted. | M | T | Planned |
| FR-BW-7 | Each board shall measure its own current, voltage and power with its INA226, at the same time. The echo server shall print its values on Serial and send them to the controller in its UDP status message; the controller shall report both with the test results. | S | T | Planned (both power monitors verified) |

## 4.6 Experiments (8.6b, 8.6c)

| ID | Requirement | Pri | Verif. | Status |
|-----|------------------------------|--|---|--------|
| FR-HUM-1 | In direct mode, the controller shall log temperature and humidity from both sensors (SHT31-D at the radio path and steam, DHT11 in the room, HS3003 in the box), RSSI, latency, losses and throughput as CSV on Serial. | S | T | Planned |
| FR-HUM-2 | The experiment shall cover: free path, thermos with cold water, 40, 60, 80 and 100 °C, and free path again; 3 repeats each, with the thermos beside the path. The next condition shall start only when the SHT31-D has recovered (HW-8), using its heater. | S | M | Planned |
| FR-HUM-3 | The radio path shall be at least the first Fresnel radius plus 5 cm above the thermos opening (≈ 86 cm at 1 m between the boards). | S | M | Planned |
| FR-REFL-1 | With a reflector behind the controller's antenna, the test shall run at every 15° from 0° to 180° in direct mode. | C | M | Planned |
| FR-REFL-2 | The results shall be shown as a polar chart (RSSI and gain in dB per angle). | C | T | Planned |

## 4.7 Measurement data and history (8.5b)

| ID | Requirement | Pri | Verif. | Status |
|-----|------------------------------|--|---|--------|
| FR-DATA-1 | All measurements shall use one format: sequence number, UTC time in ms, node, kind and value. | M | I | Planned |
| FR-DATA-2 | Each board shall send one batch per round to the collector; the batch number shall be the round number, with a CRC-32 over the values. | M | T | Planned |
| FR-DATA-3 | The collector shall answer ACK per saved batch and NACK with batch number for a damaged (`reason=crc`) or missing (`reason=missing`, detected at the deadline) batch; the sender shall resend that batch. | M | T | Planned |
| FR-DATA-4 | A batch shall be stored exactly once (duplicates acknowledged and ignored) and flushed to disk before ACK. | M | T | Planned |
| FR-DATA-5 | The controller's web page shall show interactive time graphs of all measurements (chart library served from the board's flash, no external scripts) and offer CSV export. | S | T | Planned |
| FR-DATA-6 | History shall survive a board restart (flash) and network outages (store and forward until ACK). | S | T | Planned |

## 4.8 Printing (8.7)

| ID | Requirement | Pri | Verif. | Status |
|-----|------------------------------|--|---|--------|
| FR-PRINT-1 | The controller shall scan, then join the printer's Wi-Fi Direct network and send a plain-text report to port 9100, ending with a form feed. | S | M | Planned |
| FR-PRINT-2 | The report shall list each network with SSID as text and hex, RSSI, channel and security mode, with open networks marked. | S | M | Planned |
| FR-PRINT-3 | The report shall carry the date and time; the clock shall be set by NTP on the router network (waiting at most 10 s) before switching to the printer's network. | S | M | Planned |

## 4.9 Later features

| ID | Requirement | Pri | Verif. | Status |
|-----|------------------------------|--|---|--------|
| FR-CLIENT-1 | Clients on the router network shall be listed and classified from an authorized source (the router's client list). | C | M | Planned |
| FR-LCD-1 | The web page shall offer a form whose text is shown on the LCD. | C | M | Planned |
| FR-RADAR-1 | Phase 1: board A shall step its reflector through 24 angles (15°, 360°), recording the router's beacons (RSSI, channel, UTC) in a 4 s window per angle with the motor at rest and coils off. | C | T | Planned |
| FR-RADAR-2 | Phase 1: the median per angle shall be fitted to a Gaussian with a floor, giving the router's direction θ₀, beam width σ, leakage C and contrast A/C; a sweep without reflector shall serve as reference. | C | M | Planned |
| FR-RADAR-6 | Before measuring, the radar shall home on the index (TMAG5273 + magnet, index = 180°) and self-calibrate during two full turns without radio measurements: steps per turn, background field per angle, index thresholds, direction of rotation, repeatability within ±1 step. During measurement it shall rotate continuously in one direction and re-reference the step count at every index pulse. | C | T | Planned |
| FR-RADAR-3 | The router channel shall not be locked; it shall be logged with every value, the baseline calculated per channel, and values near a channel change flagged. | C | I | Planned |
| FR-RADAR-4 | Phase 2a: slaves B and C shall listen to the router beacons in the same windows; movement shall be detected as increased variation per link against an empty-room calibration, giving a sector. | C | M | Planned |
| FR-RADAR-5 | Phase 2b: A, B and C shall take turns transmitting bursts in NTP-scheduled slots with guard times; A's reflector shall home in within the sector, one angle per round. | C | M | Planned |

# 5. Non-functional requirements

| ID | Requirement | Pri | Verif. | Status |
|-----|------------------------------|--|---|--------|
| NFR-RT-1 | `loop()` shall not block for long: web serving, scan and NTP shall be non-blocking. Accepted blocking: internet check up to 2 × 5 s, reconnect up to 20 s. | M | I | Verified |
| NFR-RT-2 | During a bandwidth series, nothing else shall run (no scan, web, NTP or HTTP), and Wi-Fi power saving shall be off on both boards. | M | I | Planned |
| NFR-RT-3 | Round-trip times shall be measured in microseconds with `esp_timer`. | M | I | Planned |
| NFR-RT-4 | Measurement activities shall be time-triggered: every window shall start at an absolute UTC time (radar phase 1: `UTC mod 6 s = 0`; phase 2b: `UTC mod 12 s = 0`), never at "interval since last time" (see `lab8_wifi_rt_design.md`). | M | I | Planned |
| NFR-RT-5 | Only one board shall transmit at a time; slots shall be separated by guard times larger than the clock offset plus drift. | M | T | Planned |
| NFR-RT-6 | Clock offset between nodes shall stay below half the guard time; a node outside the limit shall stop transmitting (fail-silent) until resynchronized. | M | T | Planned |
| NFR-RT-7 | Slot start jitter shall be below 1 ms for 99 % of slots. | S | T | Planned |
| NFR-RT-8 | Sensors shall be sampled at full UTC seconds on all boards. | S | I | Planned |
| NFR-RT-9 | Web requests, uploads, the internet check and motor movement shall only run in the idle window. | M | I | Planned |
| NFR-NET-1 | The boards shall have DHCP reservations: controller A (`lab8-esp32`) at `192.168.32.50`, echo server B (`lab8-echo`) at `192.168.32.51`, slave C (`lab8-echo2`) at `192.168.32.52`. | S | M | Planned |
| NFR-NET-2 | After a reset, the controller shall be back on Wi-Fi within about 5 s (3 s menu window + connect). | S | M | Verified |
| NFR-CLK-1 | The clock shall be within 2 minutes of the PC's clock once NTP is set. | M | T | Verified |
| NFR-MEM-1 | Stored scan results shall be limited to 40 networks and graph history to a fixed ring buffer. | M | I | Implemented / Planned |
| NFR-CODE-1 | Each program shall be one commented source file in `src/`, selected with one active `build_src_filter` line. | M | I | Verified |
| NFR-CODE-2 | Constants shall use the `K…` naming style; serial lines shall be machine-readable (`KEY value=…`). | M | I | Verified |
| NFR-TEST-1 | Every step with a testable interface shall have a Python smoke-test that returns exit code 0 on PASS. | M | I | Verified (8.3, 8.4, 8.5) |
| NFR-DOC-1 | Every step shall update the plan, the PR log and its block diagram, and add a photo when tested (resized to about 1600 px). | M | I | Ongoing |

# 6. Security requirements

| ID | Requirement | Pri | Verif. | Status |
|-----|------------------------------|--|---|--------|
| SEC-1 | No password, SSID of a private network or other secret shall be committed to git, in source code or in any file. | M | I | Verified |
| SEC-2 | Passwords shall never be printed on Serial, the LCD, the web page or the printout; typed passwords shall echo `*`. | M | T, I | Verified |
| SEC-3 | Credentials shall be stored in NVS, XOR-obfuscated with a keystream derived from the chip's MAC. | M | I | Verified |
| SEC-4 | All services shall be reachable from the local network only; no port forwarding, DDNS or own domain shall point at the home network. | M | M | Verified |
| SEC-5 | The web page shall be read-only (non-GET → 405) and answer only the local subnet (otherwise 403). | M | T | Verified |
| SEC-6 | SSIDs shall be HTML- and JSON-escaped; the page shall contain no JavaScript and send CSP, `nosniff`, `no-store` and frame-deny headers. | M | T | Verified |
| SEC-7 | The echo server shall answer only the local subnet and accept at most 1472 bytes per packet. | M | T | Planned |
| SEC-8 | HTTPS (own CA, certificate with subjectAltName, EC P-256 key stored outside git) shall be introduced before the web page accepts any input (FR-LCD-1). | M | I | Planned |
| SEC-9 | Remote access (friggebod) shall only be via VPN; controls shall have limits enforced on the device. | M | I | Planned |
| SEC-10 | Router: admin password changed from the label default, WAN administration off, system log on, WPS off. | S | M | Partly (log) |

# 7. Traceability

| Step | Requirements | Test | Status |
|---|---------|--------|-------|
| 8.1–8.3 | FR-SCAN-1…4, HW-1 | `lab8_wifi_scan_smoke_test.py` | Verified (PASS, 22 networks) |
| 8.4 | FR-CONN-1…4, NFR-NET-2 | `lab8_wifi_connect_smoke_test.py` | Verified (PASS) |
| 8.4b | FR-CRED-1…4, SEC-1…3 | 8.4 smoke-test + manual menu tests | Verified (erase not tested) |
| 8.5 | FR-WEB-1…5, NFR-RT-1, NFR-CLK-1, SEC-4…6 | `lab8_wifi_web_status_smoke_test.py`, 8.4 smoke-test, router off/on | Verified (PASS) |
| Sensor test | HW-4, HW-8 | `lab8_sensor_test.cpp` (Serial and LCD) | Verified (HS3003 and DHT11 within 0.54 °C / 0.9 % RH) |
| Motor and sensor test | HW-3, HW-4, HW-7…10 | `lab8_motor_and_sensor_test.cpp` | Verified (motor, index, three hygrometers, INA226) |
| 8.5b | FR-WEB-6…7, NFR-MEM-1 | web smoke-test (extended) | Planned |
| 8.6 | FR-BW-1…7, NFR-RT-2…3, HW-2…3, HW-6, SEC-7 | bandwidth smoke-test | Planned |
| 8.6b | FR-HUM-1…3, HW-4, HW-8, HW-9 | CSV log + graphs | Planned |
| 8.6c | FR-REFL-1…2 | polar chart | Planned |
| 8.7 | FR-PRINT-1…3 | manual (printout) | Planned |
| 8.5b data and graphs | FR-DATA-1…6, NFR-RT-4…9 | collector loss/damage test; RT jitter and offset logs | Planned |
| Features 2, 6, 7 | FR-CLIENT-1, FR-LCD-1, FR-RADAR-1…5, SEC-8, HW-7 | to be defined | Planned |

# 8. Open items

1. DHCP reservations for all three boards (NFR-NET-1).
2. Resolved: board B has its own INA226 cable (10 kΩ pull-ups on GPIO 1/2, since B has no LCD), so A and B are measured at the same time (HW-3, FR-BW-7); the motor has its own 5 V cable from the KVM switch, so the ESP32's voltage only dips 0.03 V while it turns (HW-7).
3. Router checks not yet reported: CGNAT, WAN administration, admin password, WPA mode, WPS, guest network, firmware (SEC-10).
4. Menu erase (FR-CRED-2) and wrong-password case not yet tested.
5. mDNS works (`lab8-esp32.local` opens the page); check whether the router's DNS also resolves `lab8-esp32` (`nslookup`).
