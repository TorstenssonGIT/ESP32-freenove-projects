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
- [ ] `lab8-esp32` appears in the MC888 Pro client list
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