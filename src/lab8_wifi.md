# Lab 8: Wi-Fi on the ESP32-S3

## Goal

Learn the ESP32-S3's built-in 2.4 GHz Wi-Fi features one step at a time. Reuse
the LCD already connected to the board to show progress and results. Add a
small Python test for each feature when that feature is implemented.

The ESP32-S3 has 2.4 GHz Wi-Fi built in; no external Wi-Fi module is needed.

## Test network

Use the ZTE MC888 Pro as the Lab 8 test router. Its Wi-Fi can be configured for
2.4 GHz and it has internet access. The initial scan only lists nearby networks
and does not require joining or changing the router configuration. Later
connection tests can use its 2.4 GHz network after credentials are entered
locally.

The MC888 Pro is kept for the summerhouse and is not currently used at home,
so its client list is empty. When testing at the summerhouse, connect a phone
(and optionally another device) to the router, then compare the router's client
list with the devices that were deliberately connected.

## Agreed feature list

| # | Feature | Planned result | Status |
|---|---|---|---|
| 1 | Scan nearby 2.4 GHz Wi-Fi networks | Show scan status/count on the LCD, then cycle through each SSID (up to 16 characters), RSSI, and channel; list full details and security mode on Serial, clearly identifying open networks. Do not automatically join discovered networks. | Board tested; found 12 nearby networks before the MC888 Pro was powered on |
| 2 | List and classify clients on an authorized network | Use an appropriate, authorized source for the network's client list. Do not attempt to access devices or networks without permission. | Planned |
| 3 | Connect to an internet webpage | Connect only to the user's configured or otherwise authorized Wi-Fi network and report whether an internet page can be reached. | Planned |
| 4 | Print a test page on the HP printer over Wi-Fi | Target printer: HP OfficeJet 8010 series. Investigate direct printing over the local network. First test reachability from WSL with `nc`; then identify and test a supported print protocol. | Planned |
| 5 | Run a Wi-Fi bandwidth test | Use a controlled endpoint and a bounded test; show the result/status on the LCD. | Planned |
| 6 | Browse to the ESP32 and type text for the LCD | Serve a basic page from the ESP32; submitted text is shown on the existing LCD. This is a simple browser page, not a phone app. | Planned |

## Build and test approach

Implement one feature at a time. For each step:

1. Agree on the wiring, inputs, and expected LCD/serial output.
2. Implement the feature in its own Lab 8 source file.
3. Build and test it on the ESP32-S3 before moving on.
4. Add a focused Python smoke-test when a testable interface exists.
5. Update this plan with the result and any remaining issues.

Feature 1 is implemented in `lab8_wifi_scan.cpp`. The firmware builds and the
ESP32 scan found 12 nearby networks with the MC888 Pro switched off. The LCD
cycles through each network's SSID, RSSI, and channel after each scan. Run the
Python serial smoke-test after Python and `pyserial` are available.
The serial output includes `auth=OPEN` for networks without Wi-Fi security;
connecting is a separate step and is limited to networks the user owns or has
permission to use.

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

## Details to confirm before later features

- Check printer network reachability from WSL with `nc` once its IP address and
  candidate port are known. A successful TCP connection only confirms that
  the port is reachable; it does not verify that the ESP32 can print using the
  protocol. Firewall rules, guest Wi-Fi, or client isolation may block access.
- Confirm which print protocol the HP OfficeJet 8010 series supports for direct
  printing from the ESP32; Windows Protected Print Mode availability does not
  by itself establish an ESP32-compatible protocol.
- Router/client-list method for feature 2; use only a network the user owns or
  is authorized to inventory.
- A controlled, suitable endpoint and test limits for the bandwidth test.
- Read the Wi-Fi SSID and password from the simple editable C++ declarations
  in the local-only `include/wifi_credentials.h` file. That exact path is
  excluded by this clone's `.git/info/exclude`; never commit the file, and
  check `git status` before every commit.
- Commit the exercise source and tests after they pass. Keep Wi-Fi passwords,
  printer claim codes, account credentials, and other private network details
  local; never commit them.
