"""
Lab 8.3 - Python smoke-test for lab8_wifi_scan.cpp

Reads the ESP32-S3 Serial output for ONE complete Wi-Fi scan and checks
that the scan protocol is correct.

Usage (close the PlatformIO serial monitor first, the port must be free):
    python src/lab8_wifi_scan_smoke_test.py --port COM5
    python src/lab8_wifi_scan_smoke_test.py --port /dev/ttyACM0

Requires: pip install pyserial

Checks:
    1. SCAN_BEGIN arrives within the timeout.
    2. SCAN_RESULT count=N with N >= 0.
    3. Exactly N AP lines with index, ssid_hex, ssid, rssi, channel, auth.
    4. ssid_hex decodes to the same text as ssid ('?' for non-printable / '"').
    5. RSSI between -100 and 0, channel between 1 and 14.
    6. auth is one of the known names.
    7. SCAN_END count=N matches SCAN_RESULT.
    8. Summary lists OPEN networks (reported, not a failure).

Exit code: 0 = all checks passed, 1 = a check failed or timed out.
"""

import argparse
import re
import sys
import time

# -----------------------------------------------------------------------------
#  Expected protocol
# -----------------------------------------------------------------------------
KNOWN_AUTH_MODES = {
    "OPEN", "WEP", "WPA_PSK", "WPA2_PSK", "WPA_WPA2_PSK",
    "WPA2_ENTERPRISE", "WPA3_PSK", "WPA2_WPA3_PSK", "WAPI_PSK", "UNKNOWN",
}

RE_RESULT = re.compile(r"^SCAN_RESULT count=(\d+)$")
RE_END = re.compile(r"^SCAN_END count=(\d+)$")
RE_ERROR = re.compile(r"^SCAN_ERROR code=(-?\d+)$")
RE_AP = re.compile(
    r"^AP index=(?P<index>\d+)"
    r" ssid_hex=(?P<ssid_hex>(?:[0-9A-F]{2})*)"
    r' ssid="(?P<ssid>[^"]*)"'
    r" rssi=(?P<rssi>-?\d+)"
    r" channel=(?P<channel>\d+)"
    r" auth=(?P<auth>\w+)$"
)

RSSI_MIN, RSSI_MAX = -100, 0
CHANNEL_MIN, CHANNEL_MAX = 1, 14


# -----------------------------------------------------------------------------
#  Helpers
# -----------------------------------------------------------------------------
def hex_to_display_text(ssid_hex):
    """Decode ssid_hex the same way the firmware prints ssid="..."."""
    text = ""
    for byte in bytes.fromhex(ssid_hex):
        is_printable = 32 <= byte <= 126
        text += chr(byte) if is_printable and byte != ord('"') else "?"
    return text


def check_ap_line(line, expected_index, errors):
    """Validate one AP line. Returns the parsed fields, or None."""
    match = RE_AP.match(line)
    if not match:
        errors.append(f"Malformed AP line: {line!r}")
        return None

    ap = match.groupdict()
    index = int(ap["index"])
    rssi = int(ap["rssi"])
    channel = int(ap["channel"])

    if index != expected_index:
        errors.append(f"AP index {index}, expected {expected_index}")
    if hex_to_display_text(ap["ssid_hex"]) != ap["ssid"]:
        errors.append(f"AP {index}: ssid_hex does not match ssid {ap['ssid']!r}")
    if not RSSI_MIN <= rssi <= RSSI_MAX:
        errors.append(f"AP {index}: rssi {rssi} outside {RSSI_MIN}..{RSSI_MAX}")
    if not CHANNEL_MIN <= channel <= CHANNEL_MAX:
        errors.append(f"AP {index}: channel {channel} outside {CHANNEL_MIN}..{CHANNEL_MAX}")
    if ap["auth"] not in KNOWN_AUTH_MODES:
        errors.append(f"AP {index}: unknown auth {ap['auth']!r}")
    return ap


def check_scan(lines):
    """
    Validate one scan, given the lines AFTER 'SCAN_BEGIN' up to and
    including 'SCAN_END ...'. Returns (errors, access_points).
    """
    errors = []
    access_points = []

    if not lines:
        return ["No lines after SCAN_BEGIN"], access_points

    first = lines[0]
    if RE_ERROR.match(first):
        return [f"Firmware reported {first}"], access_points

    result = RE_RESULT.match(first)
    if not result:
        return [f"Expected SCAN_RESULT, got {first!r}"], access_points
    count = int(result.group(1))

    ap_lines = lines[1:-1]
    if len(ap_lines) != count:
        errors.append(f"SCAN_RESULT count={count} but {len(ap_lines)} AP lines")

    for expected_index, line in enumerate(ap_lines):
        ap = check_ap_line(line, expected_index, errors)
        if ap:
            access_points.append(ap)

    end = RE_END.match(lines[-1])
    if not end:
        errors.append(f"Expected SCAN_END, got {lines[-1]!r}")
    elif int(end.group(1)) != count:
        errors.append(f"SCAN_END count={end.group(1)} != SCAN_RESULT count={count}")

    return errors, access_points


def read_one_scan(port, baud, timeout_s):
    """Read Serial until one full scan (SCAN_BEGIN .. SCAN_END) is captured."""
    import serial  # Imported here so the parser can be tested without pyserial

    deadline = time.monotonic() + timeout_s
    scan_lines = []
    in_scan = False

    with serial.Serial(port, baud, timeout=1) as connection:
        while time.monotonic() < deadline:
            raw = connection.readline()
            if not raw:
                continue
            line = raw.decode("ascii", errors="replace").strip()

            if line == "SCAN_BEGIN":
                in_scan = True       # Start over if we joined mid-scan
                scan_lines = []
                continue
            if in_scan:
                scan_lines.append(line)
                if line.startswith("SCAN_END"):
                    return scan_lines

    return None


def print_summary(access_points):
    print(f"Networks: {len(access_points)}")
    for ap in access_points:
        name = ap["ssid"] or "(hidden SSID)"
        print(f"  {name:<32} rssi={ap['rssi']:>4} ch={ap['channel']:>2} {ap['auth']}")

    open_networks = [ap["ssid"] or "(hidden SSID)"
                     for ap in access_points if ap["auth"] == "OPEN"]
    if open_networks:
        print(f"OPEN networks (no password): {', '.join(open_networks)}")
    else:
        print("OPEN networks: none")


# -----------------------------------------------------------------------------
#  Main
# -----------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="Lab 8 Wi-Fi scan smoke-test")
    parser.add_argument("--port", required=True, help="Serial port, e.g. COM5")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=90,
                        help="Seconds to wait for one full scan (default 90)")
    args = parser.parse_args()

    print(f"Waiting for a full scan on {args.port} (max {args.timeout:.0f} s)...")
    lines = read_one_scan(args.port, args.baud, args.timeout)
    if lines is None:
        print("FAIL: no complete scan (SCAN_BEGIN .. SCAN_END) within timeout")
        return 1

    errors, access_points = check_scan(lines)
    print_summary(access_points)

    if errors:
        print("FAIL:")
        for error in errors:
            print(f"  - {error}")
        return 1

    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
