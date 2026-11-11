# Lab 8: Wi-Fi on the ESP32-S3

## Goal

Learn the ESP32-S3's built-in 2.4 GHz Wi-Fi features one step at a time. Reuse
the LCD already connected to the board to show progress and results. Add a
small Python test for each feature when that feature is implemented. Analyse
the security of each step: which data needs protection, which measures are
taken, and which risks are accepted (see
[`lab8_wifi_security_analysis.md`](lab8_wifi_security_analysis.md)).

The ESP32-S3 has 2.4 GHz Wi-Fi built in; no external Wi-Fi module is needed.

## Test network

Use the ZTE MC888 Pro as the Lab 8 test router. Its Wi-Fi is configured for
2.4 GHz and it has internet access. The initial scan only lists nearby networks
and does not require joining or changing the router configuration. Connection
tests use its 2.4 GHz network; the credentials are entered once over the
Serial setup menu and stored on the board (see "How Wi-Fi credentials work").

The ESP32 joins as host name `lab8-esp32`, so it can be identified in the
router's client list. When testing feature 2, connect a phone (and optionally
another device) to the router, then compare the router's client list with the
devices that were deliberately connected.

The printer (feature 4) is reached through its own Wi-Fi Direct network
(`DIRECT-93-HP OfficeJet 8010`), which has no internet access.

## Agreed feature list

| # | Feature | Planned result | Status |
|---|---|---|---|
| 1 | Scan nearby 2.4 GHz Wi-Fi networks | Show scan status/count on the LCD, then cycle through each SSID (up to 16 characters), RSSI, and channel; list full details and security mode on Serial, clearly identifying open networks. Do not automatically join discovered networks. | Done. 8.1: readable code + credentials template. 8.2: readable SSID on Serial. Board tested: 12 networks (MC888 Pro off), later 19. |
| 1b | Python smoke-test for the scan | `lab8_wifi_scan_smoke_test.py` reads one scan from Serial and validates counts, SSID, RSSI, channel and auth mode; lists open networks. | Done (8.3): PASS on COM3, 22 networks, 2 open. |
| 2 | List and classify clients on an authorized network | Use an appropriate, authorized source for the network's client list. Do not attempt to access devices or networks without permission. | Planned |
| 3 | Connect to an internet webpage | Join the configured 2.4 GHz network as host name `lab8-esp32`; report IP, gateway, subnet and DNS on Serial and name + IP on the LCD; check `http://connectivitycheck.gstatic.com/generate_204` (HTTP 204 = internet OK) every 30 s. | Done (8.4): connected as `lab8-esp32` (192.168.32.6, DNS 192.168.32.1), HTTP 204 in ~300 ms; smoke-test PASS on COM3; router off shows "WiFi FAIL" and recovers. 8.4b: credentials moved to NVS (XOR-obfuscated), entered via Serial menu; smoke-test PASS. |
| 3b | Web status page on the ESP32 | Serve `http://<esp-ip>/` and `http://lab8-esp32.local/` with the latest scan result and connection/internet status (hostname, IP, gateway, subnet, DNS, HTTP result); `/api/status` returns JSON for the Python smoke-test. Feature 6 later adds the LCD text form to this same server. | Planned (8.5) |
| 4 | Print a scan report on the HP printer over Wi-Fi | Target printer: HP OfficeJet 8010 series, reached through its Wi-Fi Direct network. The ESP32 scans nearby networks, then sends a plain-text report to port 9100 (JetDirect/raw), ending with a form feed. Each network: SSID as ASCII text and as hex (same as `ssid="…"` and `ssid_hex=` on Serial; non-printable characters as `?` in the text), RSSI, channel, security; open networks marked. | PC test passed: plain text to port 9100 over Wi-Fi Direct printed correctly (no PCL needed). ESP32 firmware planned (8.6). |
| 5 | Run a Wi-Fi bandwidth test | Use a controlled endpoint and a bounded test; show the result/status on the LCD. | Planned |
| 6 | Browse to the ESP32 and type text for the LCD | Serve a basic page from the ESP32; submitted text is shown on the existing LCD. This is a simple browser page, not a phone app. | Planned |
| 7 | Wi-Fi presence detector | Detect presence from changes in Wi-Fi signal strength, using a motor-turned improvised reflector. Details to be agreed. | Planned |

## Build and test approach

Implement one feature at a time. For each step:

1. Agree on the wiring, inputs, and expected LCD/serial output.
2. Implement the feature in its own Lab 8 source file.
3. Build and test it on the ESP32-S3 before moving on.
4. Add a focused Python smoke-test when a testable interface exists.
5. Update this plan with the result and any remaining issues.

Feature 1 is implemented in `lab8_wifi_scan.cpp`. The ESP32 scan found 12
nearby networks with the MC888 Pro switched off. The LCD cycles through each
network's SSID, RSSI, and channel after each scan. The serial output includes
`auth=OPEN` for networks without Wi-Fi security; connecting is a separate step
and is limited to networks the user owns or has permission to use.

Smoke-tests are run from the PlatformIO Core CLI terminal (its Python includes
`pyserial`) with the serial monitor closed, e.g.
`python tests/lab8_wifi_connect_smoke_test.py --port COM3`.

`platformio.ini` selects the program with `build_src_filter`; only one line
may be active. When a change touches several programs, build each one.

## Existing hardware to reuse

- ESP32-S3 board with built-in 2.4 GHz Wi-Fi.
- Existing 16x2 LCD with PCF8574T I2C adapter:
  - SDA: GPIO 1
  - SCL/SCK: GPIO 2
  - I2C address: `0x27`
- Existing yellow LED: GPIO 13.

Keep the LCD wiring unchanged. NEXT and OK buttons are intended for stepping
through tests and confirming selections; select safe, unused GPIO pins before
adding button wiring. The photoresistor on GPIO 4 is part of Lab 6B and is not
required for the initial Wi-Fi scan.

## How Wi-Fi credentials work

Credentials are not stored in any file. They are typed in once over Serial
and saved in the ESP32's flash memory (NVS).

    You type in the serial monitor
            │
            ▼
    lab8_credentials.h  ── XOR with chip-specific key ──►  ESP32 flash (NVS)
                                                                 │
    Every boot: read from flash ── XOR back ──► RAM ──► WiFi.begin()

### First time on a board

1. Upload a Lab 8 Wi-Fi program and open the serial monitor
   (`pio run -t upload -t monitor`).
2. The flash is empty, so the setup menu opens by itself.
3. Choose `1` and type the router SSID and password (the password shows as `*`).
4. Optionally choose `2` for the printer's Wi-Fi Direct network, IP and port.
5. Choose `0`. The board connects.

### After that

- Nothing to do: the credentials survive power off, reset and new uploads.
- To change them: reset the board, press `c` within 3 s, use the menu.
- Menu `3` shows what is stored (passwords only as "set, hidden").
- Menu `4` or `pio run -t erase` deletes them; then repeat "First time".

### Files

| File | Contains | In git |
|---|---|---|
| `include/lab8_credentials.h` | Code: menu, XOR, flash read/write. No passwords. | Yes |
| ESP32 flash (NVS) | SSIDs and passwords, XOR-obfuscated | No (only on the board) |

## Details to confirm before later features

- Printer (resolved): the ESP32 joins the printer's Wi-Fi Direct network
  (`DIRECT-93-HP OfficeJet 8010`, printer at 192.168.223.1). Plain text sent to
  port 9100 prints directly, verified from the PC with `nc`. Open question for
  8.6: no internet on Wi-Fi Direct, so the report has no date unless the time
  is fetched (NTP) on the router network first.
- Router/client-list method for feature 2; use only a network the user owns or
  is authorized to inventory.
- A controlled, suitable endpoint and test limits for the bandwidth test.
- Credentials are entered over Serial and stored in NVS (see "How Wi-Fi
  credentials work"); no credentials file is used. `.gitignore` still covers
  `include/wifi_credentials*.h` as a safety net.
- Commit the exercise source and tests after they pass. Keep Wi-Fi passwords,
  printer claim codes, account credentials, and other private network details
  local; never commit them. Check `git status` before every commit.
- mDNS (`lab8-esp32.local`) may be blocked by some routers or guest
  networks; the IP shown on the LCD is the fallback address.

## Security

The full analysis (assets, threat actors, risk register, open issues) is in
[`lab8_wifi_security_analysis.md`](lab8_wifi_security_analysis.md)
([PDF](lab8_wifi_security_analysis.pdf)). Summary:

### Evolution of credential protection

| Level | Where the password is | Protects against | Status |
|---|---|---|---|
| 0 | Plain text in the source code | Nothing | Avoided from the start |
| 1 | Plain text in `include/wifi_credentials.h`, ignored by git | Leaks via GitHub | 8.1 |
| 2 | XOR-obfuscated in ESP32 flash (NVS), typed in over Serial | + passwords in source/firmware, readable flash dumps, copying to another board | **8.4b (current)** |
| 3 | AES-GCM in flash, key hidden in a read-protected eFuse | + decoding with source code and MAC | Documented, not implemented |
| 4 | Flash encryption + NVS encryption + Secure Boot | + attacker's own firmware on the board | Documented, not implemented |

### Credentials on the board (implemented, 8.4b)

Wi-Fi SSIDs and passwords are no longer compiled into the firmware. They are
typed in once over Serial (press `c` within 3 s after reset) and stored in the
ESP32's NVS memory by `include/lab8_credentials.h`.

Storage is **XOR-obfuscated**: each value is XORed with a keystream
(xorshift32) seeded from a hash of a salt, the chip's unique eFuse MAC address
and the NVS key name.

| Protects against | Does not protect against |
|---|---|
| Passwords in git, in the source or in the `.bin` file | Someone with the board, this source code and the chip's MAC (readable with esptool) |
| Readable SSIDs/passwords in a flash dump (`strings`) | An attacker who flashes own firmware that calls `loadCredentials()` |
| A flash copy working on another ESP32 | |

This is obfuscation, not encryption: the key is derived from information that
is not secret.

### Why MAC-XOR must never be used for transmission

`data XOR mac` sent over Wi-Fi is not protected: the MAC is sent in clear in
every Wi-Fi frame, so a listener has both the ciphertext and the key
(`sent XOR mac = data`). The key is also short (6 bytes, repeated) and never
changes, so equal messages give equal output. Data in transit needs TLS.

### Next level: AES with a hidden eFuse key (not implemented)

1. Once: burn a random 256-bit key into an eFuse key block with `espefuse.py`,
   purpose `HMAC_UP`, read-protected. Neither software nor esptool can read it.
2. Firmware: the ESP32-S3 HMAC peripheral derives an AES-256 key from the
   hidden key; credentials are stored in NVS with **AES-GCM** (encrypted and
   tamper-checked, new random nonce per write).
3. Result: a flash dump is useless even with the source code.

Not used here because the eFuse change is permanent and uses one of six key
blocks. Remaining risk: firmware flashed by an attacker can still ask the HMAC
peripheral to decrypt; stopping that needs **Secure Boot**. The full official
route is **flash encryption + NVS encryption**, which burns more eFuses, fits
poorly with PlatformIO's Arduino setup and can brick the board if done wrong.

### Risks and decisions

| Risk | Measure | Decision |
|---|---|---|
| Passwords leaked via GitHub | Credentials not in source; `.gitignore` covers `include/wifi_credentials*.h` | Fixed |
| Passwords shown on Serial/LCD/web page | Never printed; typed passwords echo `*` | Fixed |
| Passwords read from a stolen board | XOR obfuscation; unattended boards (friggebod) on a separate guest/IoT network | Reduced; AES/eFuse documented as next level |
| Internet check readable in transit | HTTPS with certificate verification (`WiFiClientSecure`, not `setInsecure()`) | Planned |
| DNS lookups, server name, IP/MAC, timing visible | Cannot be hidden in normal networking | Accepted: reveals only which server is contacted |
| ESP32 web page (8.5) over plain HTTP | Read-only, shows no secrets; smoke-test asserts no password in output | Accepted on own WPA2 network |
| Print job (8.6) unencrypted on port 9100 | Purpose is a readable paper copy, so encrypting the path protects nothing; content is broadcast SSIDs; only printer + ESP32 on the WPA2 Wi-Fi Direct network; report never contains credentials | Accepted |

Wi-Fi itself is protected by WPA2/WPA3 on the radio link; TLS protects
connections to the internet; neither protects data stored on the board.
