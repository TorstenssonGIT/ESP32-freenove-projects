"""
Lab 8.4 - Python smoke-test for lab8_wifi_connect.cpp

Reads the ESP32-S3 Serial output for ONE complete check cycle
(CHECK_BEGIN .. CHECK_END) and verifies that the ESP32 is on Wi-Fi and
reached the internet.

Usage (close the PlatformIO serial monitor first, the port must be free;
run from the PlatformIO Core CLI terminal so its Python is used):
    python src\\lab8_wifi_connect_smoke_test.py --port COM3

Checks:
    1. CHECK_BEGIN .. CHECK_END arrives within the timeout.
    2. No WIFI_CONNECT_FAIL line.
    3. WIFI_STATUS connected=1 with:
       - host name lab8-esp32 (or --hostname),
       - real ip, gateway and dns (not 0.0.0.0), valid subnet mask,
         gateway inside the ESP32's subnet,
       - RSSI between -100 and 0, channel between 1 and 14.
    4. HTTP_CHECK line present.
    5. HTTP_RESULT code=204 ok=1 with millis >= 0.
    6. CHECK_END ok=1.

Exit code: 0 = all checks passed, 1 = a check failed or timed out.
"""

import argparse
import ipaddress
import re
import sys
import time

# -----------------------------------------------------------------------------
#  Expected protocol
# -----------------------------------------------------------------------------
EXPECTED_HTTP_CODE = 204

RE_CONNECT_FAIL = re.compile(r"^WIFI_CONNECT_FAIL status=(-?\d+) reason=(\w+)$")
RE_STATUS = re.compile(
    r"^WIFI_STATUS connected=(?P<connected>[01])"
    r" hostname=(?P<hostname>\S+)"
    r" ip=(?P<ip>[0-9.]+)"
    r" gateway=(?P<gateway>[0-9.]+)"
    r" subnet=(?P<subnet>[0-9.]+)"
    r" dns=(?P<dns>[0-9.]+)"
    r" rssi=(?P<rssi>-?\d+)"
    r" channel=(?P<channel>\d+)$"
)

DEFAULT_HOSTNAME = "lab8-esp32"   # Must match KHostName in the firmware
ADDRESS_FIELDS = ("ip", "gateway", "dns")
RE_HTTP_CHECK = re.compile(r"^HTTP_CHECK url=(\S+)$")
RE_HTTP_ERROR = re.compile(r'^HTTP_ERROR text="([^"]*)"$')
RE_HTTP_RESULT = re.compile(
    r"^HTTP_RESULT code=(?P<code>-?\d+) ok=(?P<ok>[01]) millis=(?P<millis>\d+)$"
)
RE_END = re.compile(r"^CHECK_END ok=([01])$")

RSSI_MIN, RSSI_MAX = -100, 0
CHANNEL_MIN, CHANNEL_MAX = 1, 14


# -----------------------------------------------------------------------------
#  Helpers
# -----------------------------------------------------------------------------
def first_match(pattern, lines):
    """Return the first regex match among lines, or None."""
    for line in lines:
        match = pattern.match(line)
        if match:
            return match
    return None


def check_address(name, text, errors):
    """Return the IPv4 address, or None after logging an error."""
    try:
        address = ipaddress.IPv4Address(text)
    except ipaddress.AddressValueError:
        errors.append(f"{name}: invalid address {text!r}")
        return None
    if address == ipaddress.IPv4Address("0.0.0.0"):
        errors.append(f"{name} is 0.0.0.0 (no DHCP lease)")
        return None
    return address


def check_cycle(lines, expected_hostname=DEFAULT_HOSTNAME):
    """
    Validate one cycle, given the lines AFTER 'CHECK_BEGIN' up to and
    including 'CHECK_END ...'. Returns (errors, summary dict).
    """
    errors = []
    summary = {}

    fail = first_match(RE_CONNECT_FAIL, lines)
    if fail:
        errors.append(f"Wi-Fi connect failed: status={fail.group(1)} "
                      f"reason={fail.group(2)} (check SSID/password, 2.4 GHz)")

    status = first_match(RE_STATUS, lines)
    if not status:
        errors.append("No WIFI_STATUS line")
    else:
        rssi = int(status["rssi"])
        channel = int(status["channel"])
        summary.update(status.groupdict(), rssi=rssi, channel=channel)

        if status["connected"] != "1":
            errors.append("WIFI_STATUS connected is not 1")
        if status["hostname"] != expected_hostname:
            errors.append(f"hostname {status['hostname']!r}, "
                          f"expected {expected_hostname!r}")

        addresses = {field: check_address(field, status[field], errors)
                     for field in ADDRESS_FIELDS}
        try:
            network = ipaddress.IPv4Network(f"0.0.0.0/{status['subnet']}")
            ip, gateway = addresses["ip"], addresses["gateway"]
            if ip and gateway:
                local = ipaddress.IPv4Network(f"{ip}/{network.prefixlen}",
                                              strict=False)
                if gateway not in local:
                    errors.append(f"gateway {gateway} not in the ESP32's "
                                  f"subnet {local}")
        except ValueError:
            errors.append(f"Invalid subnet mask {status['subnet']!r}")

        if not RSSI_MIN <= rssi <= RSSI_MAX:
            errors.append(f"rssi {rssi} outside {RSSI_MIN}..{RSSI_MAX}")
        if not CHANNEL_MIN <= channel <= CHANNEL_MAX:
            errors.append(f"channel {channel} outside {CHANNEL_MIN}..{CHANNEL_MAX}")

    check = first_match(RE_HTTP_CHECK, lines)
    if check:
        summary["url"] = check.group(1)
    elif status:
        errors.append("No HTTP_CHECK line")

    http_error = first_match(RE_HTTP_ERROR, lines)
    if http_error:
        errors.append(f"HTTP error: {http_error.group(1)}")

    result = first_match(RE_HTTP_RESULT, lines)
    if not result:
        if status:
            errors.append("No HTTP_RESULT line")
    else:
        code = int(result["code"])
        summary.update(code=code, millis=int(result["millis"]))
        if code != EXPECTED_HTTP_CODE:
            errors.append(f"HTTP code {code}, expected {EXPECTED_HTTP_CODE}")
        if result["ok"] != "1":
            errors.append("HTTP_RESULT ok is not 1")

    end = RE_END.match(lines[-1]) if lines else None
    if not end:
        errors.append("Cycle did not end with CHECK_END")
    elif end.group(1) != "1":
        errors.append("CHECK_END ok=0")

    return errors, summary


def read_one_cycle(port, baud, timeout_s):
    """Read Serial until one full cycle (CHECK_BEGIN .. CHECK_END) is captured."""
    import serial  # Imported here so the parser can be tested without pyserial

    deadline = time.monotonic() + timeout_s
    cycle_lines = []
    in_cycle = False

    with serial.Serial(port, baud, timeout=1) as connection:
        while time.monotonic() < deadline:
            raw = connection.readline()
            if not raw:
                continue
            line = raw.decode("ascii", errors="replace").strip()

            if line == "CHECK_BEGIN":
                in_cycle = True      # Start over if we joined mid-cycle
                cycle_lines = []
                continue
            if in_cycle:
                cycle_lines.append(line)
                if line.startswith("CHECK_END"):
                    return cycle_lines

    return None


def print_summary(summary):
    if "ip" in summary:
        print(f"Wi-Fi : {summary['hostname']} ip={summary['ip']} "
              f"gateway={summary['gateway']} subnet={summary['subnet']} "
              f"dns={summary['dns']}")
        print(f"Radio : rssi={summary['rssi']} channel={summary['channel']}")
    if "code" in summary:
        print(f"HTTP  : {summary.get('url', '?')} -> {summary['code']} "
              f"in {summary['millis']} ms")


# -----------------------------------------------------------------------------
#  Main
# -----------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="Lab 8 Wi-Fi connect smoke-test")
    parser.add_argument("--port", required=True, help="Serial port, e.g. COM3")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=90,
                        help="Seconds to wait for one full cycle (default 90)")
    parser.add_argument("--hostname", default=DEFAULT_HOSTNAME,
                        help=f"Expected host name (default {DEFAULT_HOSTNAME})")
    args = parser.parse_args()

    print(f"Waiting for a full check cycle on {args.port} "
          f"(max {args.timeout:.0f} s)...")
    lines = read_one_cycle(args.port, args.baud, args.timeout)
    if lines is None:
        print("FAIL: no complete cycle (CHECK_BEGIN .. CHECK_END) within timeout")
        return 1

    errors, summary = check_cycle(lines, args.hostname)
    print_summary(summary)

    if errors:
        print("FAIL:")
        for error in errors:
            print(f"  - {error}")
        return 1

    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
