"""
Lab 8.5 - Python smoke-test for lab8_wifi_web_status.cpp

Tests the ESP32's web status page over the LOCAL network (no serial port
needed). The PC must be on the same Wi-Fi as the ESP32.

Usage (run from the PlatformIO Core CLI terminal or any Python 3):
    python tests/lab8_wifi_web_status_smoke_test.py --host 192.168.32.2
    python tests/lab8_wifi_web_status_smoke_test.py --host lab8-esp32.local

Optional: also prove that the Wi-Fi password never appears in the output.
The password is read from an environment variable, never from the command
line (which would end up in the shell history):
    PowerShell:  $env:LAB8_WIFI_PASSWORD = "..." ; python tests/...
    bash:        LAB8_WIFI_PASSWORD="..." python tests/...

Checks:
    1. GET /api/status -> 200 JSON with device, wifi, internet and scan data.
    2. Values are sane: host name, IP/gateway/DNS, subnet, RSSI, channel,
       connect time within the 20 s timeout, internet OK (HTTP 204).
    3. Scan list: SSID text matches ssid_hex, RSSI/channel/auth valid.
       Waits for the first background scan if it has not finished yet.
    4. Clock: NTP synced, ESP32 time within 2 minutes of the PC's clock,
       local time shown as CET or CEST.
    5. GET / -> 200 HTML containing the host name and no <script>.
    6. Security: no password-like JSON fields; password (if given) absent
       from JSON and HTML; security headers present; POST -> 405
       (read-only); unknown path -> 404.

Exit code: 0 = all checks passed, 1 = a check failed.
"""

import argparse
import ipaddress
import json
import os
import sys
import time
import urllib.error
import urllib.request

# -----------------------------------------------------------------------------
#  Expected values
# -----------------------------------------------------------------------------
EXPECTED_HOSTNAME = "lab8-esp32"
EXPECTED_HTTP_CODE = 204
CONNECT_TIMEOUT_MS = 20000          # KWifiConnectTimeoutMillis in the firmware
RSSI_MIN, RSSI_MAX = -100, 0
CHANNEL_MIN, CHANNEL_MAX = 1, 14
KNOWN_AUTH_MODES = {
    "OPEN", "WEP", "WPA_PSK", "WPA2_PSK", "WPA_WPA2_PSK",
    "WPA2_ENTERPRISE", "WPA3_PSK", "WPA2_WPA3_PSK", "WAPI_PSK", "UNKNOWN",
}
FORBIDDEN_KEY_PARTS = ("pass", "psk", "secret", "key")
REQUIRED_HEADERS = {
    "Cache-Control": "no-store",
    "X-Content-Type-Options": "nosniff",
    "Content-Security-Policy": "default-src 'none'",
}
PASSWORD_ENV = "LAB8_WIFI_PASSWORD"
MAX_CLOCK_DIFF_S = 120              # ESP32 clock vs PC clock


# -----------------------------------------------------------------------------
#  HTTP helper
# -----------------------------------------------------------------------------
def request(url, method="GET", timeout=10):
    """Return (status, headers, body_text). HTTP errors are returned, not raised."""
    data = b"" if method == "POST" else None
    req = urllib.request.Request(url, method=method, data=data)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as response:
            return response.status, response.headers, response.read().decode("utf-8")
    except urllib.error.HTTPError as error:
        return error.code, error.headers, error.read().decode("utf-8", "replace")


# -----------------------------------------------------------------------------
#  Checks (pure functions, testable without a board)
# -----------------------------------------------------------------------------
def hex_to_display_text(ssid_hex):
    """Decode ssid_hex the way the firmware shows SSIDs ('?' for non-ASCII)."""
    if ssid_hex == "":
        return "(hidden SSID)"
    return "".join(chr(b) if 32 <= b <= 126 else "?" for b in bytes.fromhex(ssid_hex))


def find_forbidden_keys(value, path=""):
    """Return JSON paths whose key looks like it could hold a secret."""
    found = []
    if isinstance(value, dict):
        for key, child in value.items():
            child_path = f"{path}.{key}" if path else key
            if any(part in key.lower() for part in FORBIDDEN_KEY_PARTS):
                found.append(child_path)
            found += find_forbidden_keys(child, child_path)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            found += find_forbidden_keys(child, f"{path}[{index}]")
    return found


def check_clock(device, pc_epoch, errors):
    """NTP: clock set, close to the PC's clock, Swedish time zone."""
    if device.get("ntp_synced") is not True:
        errors.append("device.ntp_synced is not true (NTP not set)")
        return
    difference = abs(device.get("epoch", 0) - pc_epoch)
    if difference > MAX_CLOCK_DIFF_S:
        errors.append(f"ESP32 clock differs {difference:.0f} s from the PC (max {MAX_CLOCK_DIFF_S})")
    local = device.get("local_time", "")
    if not (local.endswith(" CET") or local.endswith(" CEST")):
        errors.append(f"device.local_time {local!r} is not CET/CEST")


def check_status(status, errors):
    """Validate the /api/status JSON. Appends problems to errors."""
    device = status.get("device", {})
    wifi = status.get("wifi", {})
    internet = status.get("internet", {})
    scan = status.get("scan", {})

    if device.get("hostname") != EXPECTED_HOSTNAME:
        errors.append(f"hostname {device.get('hostname')!r}, expected {EXPECTED_HOSTNAME!r}")
    if not isinstance(device.get("uptime_s"), int) or device["uptime_s"] < 0:
        errors.append("device.uptime_s missing or negative")

    if wifi.get("connected") is not True:
        errors.append("wifi.connected is not true")
    addresses = {}
    for field in ("ip", "gateway", "dns"):
        try:
            address = ipaddress.IPv4Address(wifi.get(field, ""))
            if address == ipaddress.IPv4Address("0.0.0.0"):
                errors.append(f"wifi.{field} is 0.0.0.0")
            addresses[field] = address
        except ipaddress.AddressValueError:
            errors.append(f"wifi.{field} invalid: {wifi.get(field)!r}")
    try:
        prefix = ipaddress.IPv4Network(f"0.0.0.0/{wifi.get('subnet', '')}").prefixlen
        if "ip" in addresses and "gateway" in addresses:
            local = ipaddress.IPv4Network(f"{addresses['ip']}/{prefix}", strict=False)
            if addresses["gateway"] not in local:
                errors.append(f"gateway {addresses['gateway']} not in subnet {local}")
    except ValueError:
        errors.append(f"wifi.subnet invalid: {wifi.get('subnet')!r}")
    if not RSSI_MIN <= wifi.get("rssi", 1) <= RSSI_MAX:
        errors.append(f"wifi.rssi {wifi.get('rssi')} outside {RSSI_MIN}..{RSSI_MAX}")
    if not CHANNEL_MIN <= wifi.get("channel", 0) <= CHANNEL_MAX:
        errors.append(f"wifi.channel {wifi.get('channel')} outside {CHANNEL_MIN}..{CHANNEL_MAX}")
    if not 0 < wifi.get("connect_ms", -1) <= CONNECT_TIMEOUT_MS:
        errors.append(f"wifi.connect_ms {wifi.get('connect_ms')} not in 1..{CONNECT_TIMEOUT_MS}")
    if wifi.get("connect_attempts", 0) < 1:
        errors.append("wifi.connect_attempts < 1")
    if wifi.get("reconnects", -1) < 0:
        errors.append("wifi.reconnects missing")

    if internet.get("ok") is not True or internet.get("http_code") != EXPECTED_HTTP_CODE:
        errors.append(f"internet not OK: code={internet.get('http_code')} ok={internet.get('ok')}")

    count = scan.get("count", -1)
    networks = scan.get("networks", [])
    if count < 0:
        errors.append("scan not finished")
    elif len(networks) != scan.get("stored", -1) or len(networks) > count:
        errors.append(f"scan stored={scan.get('stored')} networks={len(networks)} count={count}")
    for index, network in enumerate(networks):
        label = f"scan.networks[{index}]"
        try:
            if hex_to_display_text(network["ssid_hex"]) != network["ssid"]:
                errors.append(f"{label}: ssid {network['ssid']!r} does not match ssid_hex")
        except (KeyError, ValueError):
            errors.append(f"{label}: missing or invalid ssid/ssid_hex")
        if not RSSI_MIN <= network.get("rssi", 1) <= RSSI_MAX:
            errors.append(f"{label}: rssi {network.get('rssi')}")
        if not CHANNEL_MIN <= network.get("channel", 0) <= CHANNEL_MAX:
            errors.append(f"{label}: channel {network.get('channel')}")
        if network.get("auth") not in KNOWN_AUTH_MODES:
            errors.append(f"{label}: unknown auth {network.get('auth')!r}")


def check_security(json_text, status, html, headers_list, password, errors):
    """Security checks on the responses."""
    for path in find_forbidden_keys(status):
        errors.append(f"JSON field looks like a secret: {path}")
    if password:
        if password in json_text:
            errors.append("Wi-Fi password found in /api/status")
        if password in html:
            errors.append("Wi-Fi password found in the HTML page")
    if "<script" in html.lower():
        errors.append("HTML page contains <script>")
    for name, headers in headers_list:
        for header, expected in REQUIRED_HEADERS.items():
            value = headers.get(header, "") if headers else ""
            if expected not in value:
                errors.append(f"{name}: header {header} missing or wrong ({value!r})")


# -----------------------------------------------------------------------------
#  Main
# -----------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="Lab 8.5 web status smoke-test")
    parser.add_argument("--host", required=True,
                        help="ESP32 address, e.g. 192.168.32.2 or lab8-esp32.local")
    parser.add_argument("--scan-wait", type=float, default=90,
                        help="Seconds to wait for the first scan (default 90)")
    args = parser.parse_args()

    base = f"http://{args.host}"
    password = os.environ.get(PASSWORD_ENV, "")
    errors = []

    # 1-3: JSON status, waiting for the first scan if needed.
    print(f"Reading {base}/api/status ...")
    deadline = time.monotonic() + args.scan_wait
    while True:
        code, api_headers, json_text = request(f"{base}/api/status")
        if code != 200:
            print(f"FAIL: /api/status returned HTTP {code}")
            return 1
        pc_epoch = time.time()
        status = json.loads(json_text)
        scan_done = status.get("scan", {}).get("count", -1) >= 0
        clock_set = status.get("device", {}).get("ntp_synced") is True
        if (scan_done and clock_set) or time.monotonic() > deadline:
            break
        print("  waiting 5 s for the first scan / NTP sync ...")
        time.sleep(5)
    check_status(status, errors)
    check_clock(status.get("device", {}), pc_epoch, errors)

    # 5: HTML page.
    code, html_headers, html = request(f"{base}/")
    if code != 200 or "text/html" not in html_headers.get("Content-Type", ""):
        errors.append(f"GET / returned {code} {html_headers.get('Content-Type')!r}")
    if EXPECTED_HOSTNAME not in html:
        errors.append("HTML page does not show the host name")

    # 6: Security.
    check_security(json_text, status, html,
                   [("/api/status", api_headers), ("/", html_headers)], password, errors)
    code, _, _ = request(f"{base}/", method="POST")
    if code != 405:
        errors.append(f"POST / returned {code}, expected 405 (read-only)")
    code, _, _ = request(f"{base}/no-such-page")
    if code != 404:
        errors.append(f"GET /no-such-page returned {code}, expected 404")

    # Summary
    wifi, internet, scan = status.get("wifi", {}), status.get("internet", {}), status.get("scan", {})
    print(f"Device  : {status.get('device', {}).get('hostname')} "
          f"uptime={status.get('device', {}).get('uptime_s')} s "
          f"time={status.get('device', {}).get('local_time')!r}")
    print(f"Wi-Fi   : ip={wifi.get('ip')} rssi={wifi.get('rssi')} channel={wifi.get('channel')} "
          f"connect={wifi.get('connect_ms')} ms reconnects={wifi.get('reconnects')}")
    print(f"Internet: HTTP {internet.get('http_code')} in {internet.get('millis')} ms")
    print(f"Scan    : {scan.get('count')} networks "
          f"({sum(n.get('auth') == 'OPEN' for n in scan.get('networks', []))} open)")
    print(f"Password check: {'done' if password else f'skipped ({PASSWORD_ENV} not set)'}")

    if errors:
        print("FAIL:")
        for error in errors:
            print(f"  - {error}")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
