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
- [ ] Firmware builds
- [ ] Flashed to ESP32-S3; LCD shows scan count and cycles networks
- [ ] Serial shows `SCAN_BEGIN` / `SCAN_RESULT` / `AP ...` / `SCAN_END`
- [ ] Python serial smoke-test passes (when Python + pyserial are available)

### Security checklist
- [ ] `include/wifi_credentials.h` is NOT staged (`git status`)
- [ ] `include/wifi_credentials.h` is listed in `.gitignore`
- [ ] No passwords, claim codes, or private network details in the diff

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
- [ ] Firmware builds
- [ ] Serial `AP` lines show readable SSIDs; hidden networks show `ssid=""`
- [ ] LCD output unchanged

### Security checklist
- [ ] `include/wifi_credentials.h` is NOT staged (`git status`)
- [ ] No passwords or private network details in the diff

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
- [ ] `pip install pyserial` done
- [ ] Smoke-test prints PASS against the ESP32-S3
- [ ] Summary matches the networks shown on the LCD

### Security checklist
- [ ] `include/wifi_credentials.h` is NOT staged (`git status`)
- [ ] No passwords or private network details in the diff

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
  - Reads one cycle; checks host name, ip/gateway/dns (not 0.0.0.0),
    gateway inside the subnet, RSSI, channel, HTTP 204 and `CHECK_END ok=1`.
- `platformio.ini`: environment for `lab8_wifi_connect.cpp`.
- `src/lab8_wifi.md`: feature 3 result recorded; 8.5 web status page added to the plan.

### Testing
- [ ] Firmware builds and uploads
- [ ] LCD shows `lab8-esp32` + IP, then "Internet OK" / "HTTP 204"
- [ ] `lab8-esp32` appears in the MC888 Pro client list
- [ ] `python src\lab8_wifi_connect_smoke_test.py --port COM3` prints PASS
- [ ] Router off or wrong password: LCD shows "WiFi FAIL" with a reason; recovers when fixed

### Security checklist
- [ ] `include/wifi_credentials.h` is NOT staged (`git status`)
- [ ] No passwords or private network details in the diff

