# Lab 8: Wi-Fi on the ESP32-S3 — PR descriptions

One section per step. Add a new section at the bottom for each step (8.2, 8.3, ...)
and paste that section into the GitHub PR description.
Copilot draft worked the first time 
List all available Networks 

---

## 8.1 Readable Wi-Fi scan + credentials template

**Branch:** `lab8/wifi-scan-readability-and-credentials-template`

### Summary
Refactors `lab8_wifi_scan.cpp` for readability and adds a committed template
for the local-only Wi-Fi credentials header.

### Changes
- `src/lab8_wifi_scan.cpp`
  - Header block documents purpose, hardware, and the serial protocol.
  - Code split into small, commented helpers (`showOnLcd`, `formatSsidForLcd`,
    `showNetworkOnLcd`, `printNetworkToSerial`, `reportScanFailure`,
    `reportScanResults`).
  - Named constants for LCD size, count display time, and scan arguments.
  - Includes local-only `wifi_credentials.h` (build requires the local copy).
  - Serial output format unchanged.
- `include/wifi_credentials.example.h` (new)
  - Placeholder `KWifiSsid` / `KWifiPassword`; copy to
    `include/wifi_credentials.h` locally.
- `.gitignore`: ignore `include/wifi_credentials.h` for all clones.
- `PR_DESCRIPTION.md` (new): Lab 8 PR descriptions, one section per step.

### Behavior change
None. LCD and Serial output are the same as before from lab 7.

### Testing
- [x] Firmware builds
- [X] Flashed to ESP32-S3; LCD shows scan count and cycles networks
- [X] Serial shows `SCAN_BEGIN` / `SCAN_RESULT` / `AP ...` / `SCAN_END`
- [X] Python serial smoke-test passes (when Python + pyserial are available)

### Security checklist
- [X] `include/wifi_credentials.h` is NOT staged (`git status`)
- [X] `include/wifi_credentials.h` is listed in `.gitignore`
- [X] No passwords, claim codes, or private network details in the diff

---

## 8.2 Readable SSID on Serial

**Branch:** `lab8/wifi-scan-readable-ssid-on-serial`

### Summary
Scan output on Serial now shows the SSID as text next to the hex form.

### Changes
- `src/lab8_wifi_scan.cpp`
  - New helper `printSsidAsText`: quoted SSID; non-printable characters and `"` shown as `?`.
  - `AP` lines gain `ssid="<text>"` after `ssid_hex=`.
  - `ssid_hex` kept for reliable parsing.

### Testing
- [X] Firmware builds
- [X] Serial `AP` lines show readable SSIDs; hidden networks show `ssid=""`
- [X] LCD output unchanged

### Security checklist
- [X] `include/wifi_credentials.h` is NOT staged (`git status`)
- [X] No passwords or private network details in the diff

---

## 8.3 Python smoke-test for the Wi-Fi scan

**Branch:** `lab8/wifi-scan-python-smoke-test`

### Summary
Adds a Python smoke-test that reads one full scan from Serial and validates
the scan protocol from 8.1/8.2.

### Changes
- `src/lab8_wifi_scan_smoke_test.py` (new)
  - Waits for `SCAN_BEGIN` .. `SCAN_END` (default timeout 90 s).
  - Checks `SCAN_RESULT`/`SCAN_END` counts match the number of `AP` lines.
  - Validates each `AP` line: index order, `ssid_hex` decodes to `ssid`,
    RSSI -100..0, channel 1..14, known `auth` name.
  - Prints a summary and lists OPEN networks.
  - Exit code 0 = PASS, 1 = FAIL.

### How to run
Close the serial monitor first, then:
`python src/lab8_wifi_scan_smoke_test.py --port COM5`

### Testing
- [X] `pip install pyserial` done
- [X] Smoke-test prints PASS against the ESP32-S3
- [X] Summary matches the networks shown on the LCD

### Security checklist
- [X] `include/wifi_credentials.h` is NOT staged (`git status`)
- [X] No passwords or private network details in the diff

## 8.4 Connect to Wi-Fi and check internet access (feature 3)

**Branch:** `lab8/wifi-connect-internet-check`

### Summary
New program that joins the 2.4 GHz network from `wifi_credentials.h` and
checks internet access every 30 s against
`http://connectivitycheck.gstatic.com/generate_204`, plus a Python smoke-test.

### Changes
- `src/lab8_wifi_connect.cpp` (new)
  - Host name `lab8-esp32` (set before Wi-Fi starts; shows in the router's client list).
  - Joins the configured network (20 s timeout); reconnects if Wi-Fi drops.
  - Readable failure reason on Serial and LCD (`NO_SSID_AVAIL`, `CONNECT_FAILED`, ...).
  - HTTP GET with 5 s timeout; `204` = internet OK.
  - LCD: Connecting WiFi -> host name + IP -> Internet OK/FAIL + HTTP code and time.
  - `WIFI_STATUS` reports hostname, ip, gateway, subnet, dns, rssi, channel.
  - Serial block per cycle: `CHECK_BEGIN` .. `CHECK_END ok=<0|1>`.
  - Password is never printed.
- `src/lab8_wifi_connect_smoke_test.py` (new)
  - Reads one cycle (`CHECK_BEGIN` .. `CHECK_END`) from Serial.
  -  checks host name `lab8-esp32`, ip/gateway/dns (not 0.0.0.0),
    gateway inside the subnet, RSSI, channel, HTTP 204 and `CHECK_END ok=1`.
- `platformio.ini`: environment for `lab8_wifi_connect.cpp`.
- `src/lab8_wifi.md`: feature 3 result recorded; 8.5 web status page added to the plan.

### Testing
- [X] Firmware builds and uploads
- [X] LCD shows `lab8-esp32` + IP, then "Internet OK" / "HTTP 204"
- [X] `lab8-esp32` appears in the MC888 Pro client list
- [X] `python tests/lab8_wifi_connect_smoke_test.py --port COM3` prints PASS
- [X] Router off: LCD shows "WiFi FAIL" with a reason; recovers when fixed. Wrong Password not checked.

### Security checklist
- [X] `include/wifi_credentials.h` is NOT staged (`git status`)
- [X] No passwords or private network details in the diff

## 8.4b Credentials stored on the board (NVS, XOR-obfuscated)

**Branch:** `lab8/wifi-credentials-nvs-xor`

### Summary
Wi-Fi credentials move out of the source and firmware into the ESP32's NVS,
entered once over Serial and stored XOR-obfuscated with a chip-specific
keystream. Adds a security analysis (MD + PDF) and a security section to the
Lab 8 plan.

### Changes
- `include/lab8_credentials.h` (new)
  - Serial setup menu (press `c` within 3 s after reset): router network,
    printer Wi-Fi Direct (SSID, password, IP, port), show, erase.
  - Input validation: SSID 1-32, password 8-63, IPv4, port 1-65535.
  - Passwords echo `*` and are never printed.
  - Values stored as XOR-obfuscated bytes; keystream seeded from salt +
    eFuse MAC + key name.
  - `CRED_STATUS router=<0|1> printer=<0|1>` on Serial.
- `src/lab8_wifi_connect.cpp` (new)
  - Reads credentials from NVS instead of `wifi_credentials.h`.
  - No stored router network: `WIFI_CONNECT_FAIL status=-1 reason=NO_CREDENTIALS`,
    LCD "No WiFi config / Reset, press c".
- `src/lab8_wifi_connect_smoke_test.py` (reused from 8.4)) 
- `src/lab8_wifi_scan.cpp`: removed unused `#include "wifi_credentials.h"`.
- `include/wifi_credentials.example.h`: removed (no longer used by any program).
- `src/lab8_wifi_security_analysis.md` + `.pdf` (new): assets, threat actors,
  protection layers, evolution levels 0-4, XOR obfuscation and its limits,
  why MAC-XOR fails for transmission, print job analysis, AES/eFuse next
  level, risk register, open issues.
- `src/lab8_wifi.md`: credentials how-to and Security section.
- `README.md`: credentials section points to the Serial setup.
- `platformio.ini`: environment for `lab8_wifi_connect.cpp`.

### Testing
- [x] Firmware builds (connect and scan, without `wifi_credentials.h`)
- [x] Credentials entered via the Serial menu are saved (`CRED_STATUS router=1 printer=1`)
- [x] After reset (no `c`), board connects from NVS; smoke-test PASS on COM3
- [x] Menu 3 shows SSIDs, passwords only as "(set, hidden)"
- [ ] Menu 4 erases all credentials

### Security checklist
- [x] No passwords in the diff
- [x] No passwords visible in the serial log
- [x] `include/wifi_credentials.h` not staged

## 8.x Wiring and block diagrams

**Branch:** `lab8/diagrams`

### Summary
Adds a wiring diagram and one block diagram per Lab 8 step as SVG files,
shown directly on GitHub, plus a Diagrams section in the plan.

### Changes
- `src/lab8_wiring.svg` (new): LCD (I2C GPIO 1/2, 5V, GND), LED on GPIO 13
  via resistor, USB-C to PC; board B for 8.6 without wiring.
- `src/lab8_block_hardware.svg` (new): ESP32-S3 internal blocks (CPU,
  flash/NVS, PSRAM, eFuse, timer, USB/UART, I2C, GPIO, Wi-Fi radio) and the
  connected hardware and network devices per interface.
- `src/lab8_block_overview.svg` (new): all steps, router, internet, printer.
- `src/lab8_block_8_1_scan.svg`, `..._8_4_connect.svg`,
  `..._8_4b_credentials.svg`, `..._8_5_web.svg` (new): implemented steps.
- `src/lab8_block_8_6_bandwidth.svg`, `..._8_7_print.svg` (new): planned steps.
- `src/lab8_wifi.md`: Diagrams section.

### Testing
- [X] All SVGs render on GitHub
- [X] Wiring diagram matches the board (VCC, resistor value)

## Photos: board A set-up, sensors, LCD and web status page

**Branch:** `lab8/photos`

### Changes
- `src/lab8_photo_of_board_set-up.jpg`: board A with LCD, Modulino Thermo and DHT11
- `src/lab8_photo_of_sensors_closeup.jpg`: sensor wiring close-up
- `src/lab8_photo_of_lcd_sensor_readings.jpg`: LCD with HS3003 and DHT11 readings
- `src/lab8_photo_of_lcd_connected_to_internet.jpg`: LCD with "Internet OK" (8.4/8.5)
- `src/lab8_screenshot_web_status.png`: 8.5 status page, neighbouring SSIDs hidden
- `src/lab8_wifi.md`: new "Photos" section

### Checks
- [X ] All images show on GitHub (file names and case match the MD)
- [ X] Neighbouring networks' SSIDs hidden in the screenshot
- [ X] No passwords or other private details visible in the photos
- [ X] Images under ~500 kB each

### Security checklist
- [X] No passwords or private network details in the diagrams

## 8.5 Web status page on the ESP32

**Branch:** `lab8/wifi-web-status-page`

### Summary
New program that serves a read-only status page on the local network:
connection details, internet check, a background scan of nearby networks, and
connection timing. Same data as JSON for a Python smoke-test that also checks
the security decisions.

### Changes
- `src/lab8_wifi_web_status.cpp` (new)
  - Credentials from NVS (8.4b), host name `lab8-esp32`, mDNS
    `http://lab8-esp32.local/`.
  - `GET /` HTML page (refreshes every 15 s), `GET /api/status` JSON.
  - Internet check every 30 s (8.4 serial protocol unchanged, so the 8.4
    smoke-test still passes); background scan every 60 s without
    disconnecting and without blocking the web server.
  - Clock: NTP (`se.pool.ntp.org`, `pool.ntp.org`) with the Europe/Stockholm
    rule (CET/CEST); local time on the page and in JSON (`ntp_synced`,
    `epoch`, `local_time`); `NTP_SYNC` on Serial once set.
  - Timing: connect time, connect attempts, reconnections, uptime
    (`WIFI_CONNECTED`, `WIFI_STATS` on Serial; also on the page and in JSON).
  - LCD: "Internet OK/FAIL" + the IP address to browse to.
  - Security: read-only (non-GET -> 405), local subnet only (403 otherwise),
    no password fields, every SSID HTML/JSON-escaped (SSIDs are chosen by
    strangers and could contain `<script>`), no JavaScript, security headers
    (CSP `default-src 'none'`, `nosniff`, `no-store`, `DENY` framing).
- `tests/lab8_wifi_web_status_smoke_test.py` (new)
  - Over the network (`--host`), no serial port needed.
  - Validates JSON values, scan entries (`ssid` matches `ssid_hex`), HTML page.
  - Clock: NTP synced, within 2 minutes of the PC's clock, CET/CEST.
  - Security: no password-like fields; password from `LAB8_WIFI_PASSWORD`
    absent from JSON and HTML; no `<script>`; headers present; POST -> 405;
    unknown path -> 404.
- `src/lab8_block_8_5_web.svg` (new): block diagram for this step.
- `src/lab8_wifi.md`: row 3b status, 8.5 paragraph, two new risks (malicious SSID, NTP spoofing).
- `tests/lab8_wifi_scan_smoke_test.py`: moved from `src/` (repo convention: tests in `tests/`).

### Testing
- [x] Firmware builds and uploads
- [x] Page opens at `http://<ip>/` from a PC on the MC888 network
- [x] Page opens at `http://lab8-esp32.local/` (or note if mDNS is not resolved)
- [x] `python tests/lab8_wifi_web_status_smoke_test.py --host <ip>` prints PASS, with `LAB8_WIFI_PASSWORD` set (192.168.32.2, 2026-11-18)
- [x] Serial shows `NTP_SYNC ok=1` with correct local time
- [X] 8.4 smoke-test still PASS with this firmware (HTTP 204 in 82 ms)
- [x] Block diagram updated (`src/lab8_block_8_5_web.svg`)
- [X] Router off/on: connection lost (scan found 0 networks), reconnected by itself on attempt 7; page works again by IP and lab8-esp32.local

### Security checklist
- [x] No passwords in the diff
- [x] Page shows no passwords; smoke-test password check done

## Sensor test: Modulino Thermo (HS3003) + DHT11

**Branch:** `lab8/sensor-test-modulino-dht11`

### Summary
First sensors on board A: two hygrometers read side by side, as a basis for
the humidity measurements (FR-HUM) and the DHT11 recovery check (HW-8).

### Changes
- `src/lab8_sensor_test.cpp` (new)
  - I2C scan of the 3.3 V sensor bus (Wire1, SDA GPIO 47, SCL GPIO 21,
    100 kHz) at start: `I2C_SCAN ... found=<addresses>`.
  - HS3003 (Modulino Thermo) read directly over I2C (measurement request,
    4 bytes, status check); address found automatically (0x44 or 0x45).
  - DHT11 on GPIO 14 (Adafruit DHT library), read every 2 s.
  - Serial block every 2 s: `SENSOR_BEGIN` / `HS3003` / `DHT11` / `DIFF` /
    `SENSOR_END`; both readings on the LCD.
- `src/lab8_wiring_sensors.svg` (new): sensor wiring for board A
  - Modulino on its own 3.3 V I2C bus with 10 kΩ pull-ups to 3V3 (the
    Modulino has none; the LCD bus runs at 5 V).
  - DHT11: VCC 3V3, DATA GPIO 14 (10 kΩ pull-up on the module), GND.
  - INA229 on SPI (GPIO 39–42), shunt in the USB cable's VBUS (soldering
    pending).
- `platformio.ini`: sensor test line active; `lib_deps` adds
  `adafruit/DHT sensor library` and `adafruit/Adafruit Unified Sensor`.

### Results (board A, 2026-11-19)
- I2C scan: Modulino Thermo at **0x44**, so the SHT31-D will use 0x45.
- HS3003 21.94 °C / 35.4 % RH, DHT11 21.4 °C / 36.3 % RH.
- Difference −0.54 °C / +0.9 % RH (DHT11 tolerance ±2 °C / ±5 % RH).

### Testing
- [x] Builds and uploads
- [x] I2C scan finds the Modulino
- [x] Both sensors `ok=1` every 2 s
- [x] LCD shows both readings
- [ ] INA229 wired and read (after soldering)

### Security checklist
- [x] No passwords in the diff (no Wi-Fi in this sketch)

## Motor and sensor test: 28BYJ-48, TMAG5273, HS3003, SHT31-D, DHT11, INA226

**Branch:** `lab8/motor-sensor-test`

### Summary
All radar and climate hardware on board A in one test: the motor turns one
revolution back and forth; the TMAG5273 looks for the index magnet while it
turns; at rest (coils off) the three hygrometers and the INA226 are read.

### Changes
- `src/lab8_motor_and_sensor_test.cpp` (new)
  - Motor: AccelStepper HALF4WIRE on GPIO 4, 6, 5, 7 (IN1, IN3, IN2, IN4),
    4076 half steps per turn, 400 half steps/s, coils off between moves.
  - Two I2C buses scanned at start: LCD bus (GPIO 1/2) and the 3.3 V sensor
    bus (GPIO 47/21).
  - TMAG5273 (0x35): continuous X/Y/Z, ±80 mT, read every 10 ms while
    turning; strongest field and its motor position (`INDEX_PEAK`).
  - HS3003 (0x44, raw bytes printed), SHT31-D (0x45, CRC-checked), DHT11
    (GPIO 14): read at rest.
  - INA226 on the LCD bus, found by its ID (0x5449/0x2260) at 0x40–0x4F:
    `POWER_MOVE` (average and peak while turning) and `POWER_REST`.
- `src/lab8_wiring_sensors.svg` (updated): board A wiring as verified.
- `platformio.ini`: motor and sensor test active; `waspinator/AccelStepper`.
- `src/lab8_wifi_requirements.md` + `.pdf`: HW-3 INA226 (I2C, LCD bus),
  HW-7 motor on GPIO 4–7, HW-9 and HW-10 verified, traceability.
- `src/lab8_wifi_rt_design.md` + `.pdf`: INA229 → INA226.
- `src/lab8_kk2_design_report.md` + `.pdf`: sensors, actuators, results,
  problems and solutions updated.

### Results (board A, 2026-11-21)
- Motor: 4076 half steps in 12.2 s, back to position 0 every time.
- TMAG5273: background 0.04 mT; the magnet gives up to 80 mT per axis.
- HS3003 22.5 °C / 37 %, SHT31-D 22.3 °C / 36 %, DHT11 21.2 °C / 38 % RH.
- INA226 at rest: 5.27 V, 86–90 mA, ≈ 460 mW.
- Motor through the shunt: 294 mA (max 390), 1.5 W; motor 5 V taken before
  the shunt: 98 mA, 508 mW (the INA226 then measures the ESP32 only).

### Problems found and solved
- SHT31-D and Modulino both at 0x44 → SHT31-D address jumper to 0x45.
- The power monitor boards are INA226 (I2C) at 0x44, not INA229 (SPI) →
  moved to the LCD bus; this also fixed the Modulino's humidity (1–2 %).
- Motor current through the shunt → motor 5 V from IN+.

### Testing
- [x] Builds and uploads
- [x] Both I2C buses scanned; all devices found
- [x] Motor back and forth, returns to 0
- [x] Index peak follows the magnet
- [x] Three hygrometers agree within about 1.5 °C / 2 % RH
- [x] INA226 voltage, current and power plausible; positive current
- [X] Motor on its own 5 V supply, connected to IN+ so the measurement is not included the MOTOR, only the draver board. 

### Security checklist
- [x] No passwords in the diff (no Wi-Fi in this sketch)

## Lab 8 – KK2: design report, documentation, motor and sensor test

### Summary
Everything for KK2: the design report (steps 1–6), the design documents, and the
verified motor and sensor set-up on board A.

### Changes
- **Design report** `lab8_kk2_design_report.md/.pdf` (new): PMWR concept, requirements,
  rig and architecture, beam width analysis, code chapters (connect, web page, scanning),
  results, problems and solutions, future prototype with BoM (2 229 kr), AI and IPR statements.
- **Documents** (new or updated): requirements, real-time design, radar reflector,
  risk analysis (section 10, network exposure).
- **Code**: `lab8_motor_and_sensor_test.cpp` (new): motor, TMAG5273 index, HS3003,
  SHT31-D, DHT11, INA226 on the LCD bus; demo mode. `lab8_wifi_scan.cpp`: comment fixes.
- **Diagrams**: overview (boards A/B/C, BoM, Wi-Fi vs 5G), sensor wiring, future
  prototype, timing diagrams, 8.6 and 8.7 block diagrams.
- **Photos**: motor and sensor set-up, Serial monitor screenshot.
- `lab8_wifi.md`: new goal (PMWR), test network, feature rows 7–8, AI and IPR sections.
- `platformio.ini`: motor and sensor test active, AccelStepper.

### Results (board A)
- Motor 4076 half steps per turn; index background 0.04 mT.
- HS3003, SHT31-D and DHT11 agree within about 1.5 °C / 2 % RH.
- INA226: 5.27 V / 85 mA / ≈ 450 mW at rest; with the motor on its own USB-C cable the
  dip while turning dropped from 0.16 V to 0.03 V.

### Checks
- [x] Builds, uploads and runs on board A
- [x] All I2C devices found on both buses
- [x] Files verified with SHA-256 checksums
- [x] No passwords; neighbouring SSIDs hidden; no GPS in photos

## Lab 8 – KK2 update after the merge

### Summary
Changes made after the KK2 merge: the motor's own power supply, board B wired
and verified, and documentation improvements.

### Changes
- **Motor supply:** own USB-C power cable from the KVM switch (shared GND);
  the ESP32's voltage dip while the motor turns dropped from 0.16 V to 0.03 V.
- **Board B:** own INA226 cable (10 kΩ pull-ups on GPIO 1/2, since B has no LCD)
  and HC-SR501 PIR (OUT GPIO 14, jumper on H); 5.30 V / 41.6 mA / 221 mW at rest.
- **Requirements:** HW table with Item and Description columns; HW-2, HW-3,
  HW-7, HW-11 and FR-BW-7 updated; open item 2 resolved.
- **Design report:** BoM with Item and Description columns, kit parts listed;
  board B results; problems "Board B's INA226 not found" (pull-ups, broken jumper)
  and motor supply; first-person background; readable table columns in the PDFs.
- **Diagrams:** wiring (PC for data, KVM switch feeding both cables), overview
  (board B no longer planned), 8.6 (INA226 on both boards).
- **Photo:** boards A and B together.

### Checks
- [x] All files verified with SHA-256 checksums
- [x] Board A: motor and sensor test unchanged
- [x] Board B: INA226 found and measuring
- [x] No passwords; no GPS in the photo

## Lab 8: board B hardware test (INA226 + PIR)

### What
- `src/lab8_board_b_test.cpp`: test sketch for board B (lab8-echo, N16R8).
  - Scans the I2C bus on GPIO 1/2 and finds the INA226 by its TI ID.
  - Prints a POWER line (V, mA, mW) every 2 s.
  - Prints a PIR line on every change of the HC-SR501 output (GPIO 14), with a 60 s warm-up flag.
- `platformio.ini`: `build_src_filter` set to the board B test.

### Hardware
- INA226 on GPIO 1/2 with 10 kΩ pull-ups to 3V3; shunt R100 = 0.1 Ω.
- HC-SR501: VCC 5 V, OUT GPIO 14, GND; jumper H; Tx and Sx fully anticlockwise.

### Test results
- `INA226 ok=1 addr=0x44 manufacturer=0x5449 device=0x2260`
- At rest: 5.30 V / ~41 mA / ~215 mW (a peak of ~230 mW every ~14 s from board activity).
- Wire test: GPIO 14 reads 0 and 1 correctly.
- PIR: 1 on movement, 0 about 1 s after movement stops (after the pots were turned fully anticlockwise).

### Problems solved
- INA226 not found: no pull-ups, and a broken SCL jumper wire.
- PIR stuck high: the hold time was too long and the range too wide; fixed by turning Tx and Sx fully anticlockwise.

### Notes
- Upload with `--upload-port COM4 --monitor-port COM4`.
- Before uploading to board A again, change `build_src_filter` back to the motor test.
- No credentials in this PR.

## Lab 8: 8.6 bandwidth test (router mode), verified

### What
- `src/lab8_bandwidth.cpp`: one firmware for both boards; the role is set by a jumper.
  - GPIO 42 to GND: echo server (board B, `lab8-echo`), UDP port 5001, echoes packets unchanged, answers `STATUS?` with uptime and INA226 power; local subnet only, max 1460 bytes.
  - GPIO 42 open: controller (board A, `lab8-esp32`, LCD); finds the echo server by mDNS; 100 packets per size 32…1460 bytes, 500 ms timeout, sequence number and content checked; min/median/average/max RTT (`esp_timer`), losses, throughput, power of both boards.
  - Wi-Fi power saving off on both boards. Credentials from NVS (setup menu, `c` at boot).
- `platformio.ini`: `build_src_filter = -<*> +<lab8_bandwidth.cpp>`
- `report/lab8_wifi_requirements.md` + `.pdf`: FR-BW-1…4, 6, 7, NFR-RT-2…3, SEC-7 updated; 1472 → 1460.

### Test results (router mode, RSSI −61/−62 dBm, channel 11)
| Size (B) | Lost | Median RTT (ms) | Avg RTT (ms) | Max RTT (ms) | Throughput (kbit/s) |
|---|---|---|---|---|---|
| 32 | 0 | 6.5 | 7.4 | 25.4 | 69 |
| 64 | 0 | 5.4 | 6.5 | 13.5 | 157 |
| 128 | 0 | 6.0 | 7.0 | 14.5 | 293 |
| 256 | 0 | 6.0 | 7.3 | 21.2 | 563 |
| 512 | 0 | 7.5 | 8.2 | 21.9 | 1002 |
| 1024 | 0 | 7.5 | 8.5 | 31.3 | 1922 |
| 1460 | 0 | 8.2 | 9.0 | 17.5 | 2588 |

Throughput = 2 × size × 8 / average RTT (one packet at a time, so this is a latency-bound figure, not the link's maximum).

Power during a series: A ≈ 0.92–0.96 W (≈ 180 mA), B ≈ 0.67 W (≈ 129 mA). B at rest without Wi-Fi: 0.22 W.
B echoed 705 of 705 packets (5 warm-up + 7 × 100), `echo_restarted=0`.

### Problems solved
- 1472-byte packets all "lost": the Arduino UDP library receives at most 1460 bytes, so B echoed them cut short. Largest size changed to 1460; a `changed=` count now separates cut/altered packets from real losses.
- Compiler warnings: `udp.flush()` → `udp.clear()`; `(int)` casts for RSSI/channel in printf.

### Not yet
- Direct mode (FR-BW-5), web page output (FR-BW-4), DHCP reservation .51 (B got .6; mDNS finds it).

### Notes
- No credentials in this PR. The router SSID is not in the docs.

## Lab 8: feature 2, client list verified (manual)

### What
- `report/lab8_wifi_requirements.md` + `.pdf`: FR-CLIENT-1 verified; test table row for feature 2.
- `lab8_wifi.md`: feature 2 marked done; open item resolved; screenshot added.
- `lab8_screenshot_router_client_list.png`: router client page, MAC addresses hidden, other devices cropped.

### Test
- Source: the router's own client page (own network, authorized). Columns: device name, MAC address, IP address, Wi-Fi band, action (manage/block).
- Result: `lab8-esp32` (192.168.32.2) and `lab8-echo` (192.168.32.6) listed by host name on 2.4 GHz; no wired clients. Other household devices are not part of the test.

### Notes
- No MAC addresses, personal device names or credentials in this PR.

## Lab 8: movement monitor, slave B reports RSSI + PIR + power (phase 2a, first result)

### What
- `src/lab8_bandwidth.cpp`: movement monitor added to the 8.6 firmware (same build on both boards, role jumper GPIO 42 on B).
  - Both boards read the router's RSSI every 100 ms; each 1 s window gives median and spread.
  - B samples its PIR (GPIO 14) and answers `MEAS?` with its window, PIR state and INA226 power.
  - A: `m` starts/stops the monitor and prints one `MOVE` line per second for both links (LCD shows both); `r` still runs the bandwidth test.
  - A skips the bandwidth series when B doesn't answer `STATUS?` (no 6-minute timeout run); fallback address 192.168.32.6; LCD buffer warning fixed.
- `src/lab8_wifi.md`: new section "Movement monitor test", feature 7 status, three photos.
- `src/lab8_wifi_requirements.md` + `.pdf`: HW-11 verified, FR-RADAR-4 first result, test table row.
- `src/lab8_photo_of_router_in_livingroom_on_sofa.jpg`, `src/lab8_photo_of_echo_server_in_livingroom_on_sideboard.jpg`, `src/lab8_photo_of_echo_server_in_bathroom_on_washing_machine.jpg` (1600 px, no metadata).

### Test
Layout: links form a V from the router. A ≈ 4 m from the router, B on the washing machine ≈ 12–13 m from the router.

| Period | A–router | B–router | B's PIR |
|---|---|---|---|
| Still | −61 dBm, spread ≈ 0.4 dB | −56 dBm, spread ≈ 0.1 dB | partly active |
| Walking across A's leg | down to −64…−65 dBm | unchanged | quiet |
| Walking across B's leg | stays ≈ −65 dBm | spread up to 4.8 dB, down to −60.7 dBm | active |

- Only the link whose leg is crossed reacts, so the reacting link gives the direction of movement from the router.
- With B close to the router (−43 dBm) its link hardly reacted (spread max 0.8 dB).
- The fixed 2.0 dB spread threshold catches only part; empty-room calibration with a level-drop criterion is next.

### Problems solved
- `b_ok=0`: B still had the 8.6 firmware (no `MEAS?`); reflashed.
- A used fallback .51 when mDNS failed after B's power cycle; fallback now .6, and the series is skipped if B is missing.

### Notes
- No credentials, SSIDs or MAC addresses in this PR.

## Lab 8: movement monitor tests 2–4, empty-room calibration, live web page with graphs

### What
- `src/lab8_bandwidth.cpp`: empty-room calibration (`k`, see below), and board A serves a live movement page at `http://lab8-esp32.local/` (or A's IP): three status boxes (link A, link B, PIR at B), graphs of RSSI and spread for the last 3 minutes with the threshold and PIR bands, updated every second. `/api/move` gives the same data as JSON. Read-only, local subnet only, CSP allows script only from the ESP32. The monitor starts by itself after the boot series.
- `src/lab8_wifi.md`: test 2 (B behind the washing machine, V layout) and test 3 (B in the kitchen, nearly in line with A), each with table, findings and chart; test 4 (calibrated); photo of B in the kitchen and a screenshot of the live page.
- `src/lab8_chart_move_monitor_v_layout.png`: RSSI, spread and PIR for test 2.
- `src/lab8_chart_move_monitor_inline_layout.png`: RSSI and spread for test 3.
- `src/lab8_screenshot_move_monitor_web_page.png`: the live page on A after calibration.
- `src/lab8_photo_of_echo_server_on_kitchen_counter.jpg`: B in the kitchen (tests 3 and 4), 1600 px, no metadata.
- `src/lab8_wifi_requirements.md` + `.pdf`: FR-RADAR-4 status with tests 2–4.

### Test 2 (both links at −62 dBm)
| Phase | A–router | B–router | B's PIR |
|---|---|---|---|
| Still | −62 dBm | −62 dBm | quiet |
| Across A's leg | −65…−68 dBm, spread up to 4.8 dB | unchanged | quiet |
| Across B's leg | ≈ −66 dBm, flat | dip to −75 dBm, spread up to 5.0 dB | active |
| Still, person at B | recovers | back to −61.5 dBm | active |

### Test 3 (B ≈ 1 m beside the line router–A)
| Phase | A–router | B–router |
|---|---|---|
| At the desk | −58.5 dBm | −65.5 dBm |
| Crossing at 3 m | −62 dBm (−4 dB) | −66 dBm, small dips |
| Crossing at 9 m | −67…−69 dBm (−10 dB) | −66 dBm, hardly moved |
| Back at the desk | −56…−58 dBm | −65 dBm |

A person within about 1 m of a board changes its link more (up to ≈ 10 dB) than a crossing (3–6 dB), like holding a car key to your head. Baselines must be taken with nobody near a board. Links 1 m apart on nearly the same line did not separate; the V layout did.

### Calibration and test 4
- `k` on A: 30 s to leave, 30 s empty-room recording. Per link: baseline level (median) and calm spread (max). Movement = level ≥ 3 dB below baseline, or spread > calm spread + 1 dB. `MOVE` lines get `a_drop`/`b_drop`; the web page shows baselines and thresholds as dashed lines.
- Test 4 (kitchen layout): 140 s quiet with no false alarm; walking at 9 m: A drop 4–5 dB, B no alarm; walking at 3 m: A drop 6 dB, B drop 3–4 dB.
- The calibrated monitor reliably answers "is someone moving?"; "where?" needs separate paths (V layout).

### Problems solved
- PIR at B triggered every 2.5–3 s with nobody there: warm air from a water-heated towel rail and a warm pipe (a mug over the PIR stopped it, so not radio). Fixed by aiming the PIR away.
- Terminal copy lost most of the log: logged with `--filter log2file` instead.
- B's PIR next to the fridge in the kitchen was active the whole time (warm surfaces); not a reference in tests 3 and 4.

### Notes
- Web page tested in a browser with test data (light, dark, phone width); on board A: page works at http://lab8-esp32.local/ with baselines and thresholds after calibration (screenshot added).
- No credentials, SSIDs or MAC addresses in this PR.

## Note: AI support

This project was developed with assistance from Claude (Anthropic), which
supported coding, documentation and diagram creation.
The ideas, concepts, analysis and design decisions are the author's own. The
author has reviewed and tested the code, made selected modifications and
refinements, and clarified comments where appropriate.
Git and GitHub repositories, including version control and project
management, have been managed and controlled by the author.