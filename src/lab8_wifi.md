# Lab 8: Wi-Fi on the ESP32-S3

## Goal

Learn the ESP32-S3's built-in 2.4 GHz Wi-Fi features one step at a time. Reuse
the LCD already connected to the board to show progress and results. Add a
small Python test for each feature when that feature is implemented.

The ESP32-S3 has 2.4 GHz Wi-Fi built in; no external Wi-Fi module is needed.

## Agreed feature list

| # | Feature | Planned result | Status |
|---|---|---|---|
| 1 | Scan nearby 2.4 GHz Wi-Fi networks | Show scan progress and network results/status on the LCD and serial monitor. | Next |
| 2 | List and classify clients on an authorized network | Use an appropriate, authorized source for the network's client list. Do not attempt to access devices or networks without permission. | Planned |
| 3 | Connect to an internet webpage | Connect to the configured Wi-Fi and report whether an internet page can be reached. | Planned |
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

The first implementation step is feature 1: scan for nearby 2.4 GHz networks
and report status/results. No Lab 8 firmware or Python test has been added yet.

## Existing hardware to reuse

- ESP32-S3 board with built-in 2.4 GHz Wi-Fi.
- Existing 16x2 LCD with PCF8574T I2C adapter:
  - SDA: GPIO 1
  - SCL/SCK: GPIO 2
  - I2C address: `0x27`
- Existing yellow LED: GPIO 13.

Keep the LCD wiring unchanged. Decide whether NEXT/OK buttons are needed, and
select safe, unused GPIO pins, before adding button wiring. The photoresistor
on GPIO 4 is part of Lab 6B and is not required for the initial Wi-Fi scan.

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
- Wi-Fi credentials should be supplied locally during setup and must not be
  committed to the repository.
- Keep any printer access/helper code that you do not want on GitHub outside
  the repository. If it must live in the project folder, add its path to the
  local `.git/info/exclude` file (not only `.gitignore`) and verify `git status`
  before committing. Never commit printer claim codes, account credentials, or
  other secrets.
