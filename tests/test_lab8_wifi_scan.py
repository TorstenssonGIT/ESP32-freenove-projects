"""Hardware smoke test for Lab 8 Wi-Fi scanning over the USB serial port."""

import argparse
import re
import sys
import time

try:
    import serial
except ImportError as error:
    raise SystemExit(
        "Missing pyserial. Install the test dependency with "
        "'python -m pip install -r requirements-test.txt'."
    ) from error


SCAN_RESULT = re.compile(r"^SCAN_RESULT count=(\d+)$")
SCAN_END = re.compile(r"^SCAN_END count=(\d+)$")
ACCESS_POINT = re.compile(
    r"^AP index=(\d+) ssid_hex=([0-9A-Fa-f]*) "
    r"rssi=(-?\d+) channel=(\d+) auth=([A-Z0-9_]+)$"
)


def read_scan(port: str, baud: int, timeout: float) -> tuple[int, list[str]]:
    deadline = time.monotonic() + timeout
    with serial.Serial(port, baudrate=baud, timeout=1) as connection:
        connection.reset_input_buffer()
        scan_started = False
        expected_count = None
        access_points = []

        while time.monotonic() < deadline:
            line = connection.readline().decode("utf-8", errors="replace").strip()
            if line == "SCAN_BEGIN":
                scan_started = True
                expected_count = None
                access_points = []
                continue
            if not scan_started or not line:
                continue

            result_match = SCAN_RESULT.fullmatch(line)
            if result_match:
                expected_count = int(result_match.group(1))
                continue

            ap_match = ACCESS_POINT.fullmatch(line)
            if ap_match:
                index, ssid_hex, rssi, channel, _auth = ap_match.groups()
                if len(ssid_hex) % 2:
                    raise AssertionError(f"SSID hex has odd length: {ssid_hex!r}")
                bytes.fromhex(ssid_hex)
                assert int(rssi) in range(-127, 1), f"Invalid RSSI: {rssi}"
                assert int(channel) in range(1, 15), f"Invalid 2.4 GHz channel: {channel}"
                access_points.append((int(index), line))
                continue

            end_match = SCAN_END.fullmatch(line)
            if end_match:
                reported_count = int(end_match.group(1))
                if expected_count is None:
                    raise AssertionError("Scan ended without a SCAN_RESULT line")
                if reported_count != expected_count:
                    raise AssertionError(
                        f"Result count {expected_count} differs from end count "
                        f"{reported_count}"
                    )
                if len(access_points) != reported_count:
                    raise AssertionError(
                        f"Firmware reported {reported_count} networks but emitted "
                        f"{len(access_points)} AP records"
                    )
                indices = [index for index, _ in access_points]
                if indices != list(range(reported_count)):
                    raise AssertionError(f"AP indices are not sequential: {indices}")
                if reported_count == 0:
                    raise AssertionError(
                        "No Wi-Fi networks found; verify nearby 2.4 GHz coverage "
                        "and rerun the smoke test."
                    )
                return reported_count, [record for _, record in access_points]

    raise TimeoutError(
        "Timed out waiting for a complete scan. Confirm Lab 8 scan firmware is "
        "running and the serial port/baud rate are correct."
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Serial port, e.g. COM3")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=45.0)
    args = parser.parse_args()

    count, access_points = read_scan(args.port, args.baud, args.timeout)
    print(f"PASS: received {count} valid 2.4 GHz access point records.")
    for access_point in access_points:
        print(access_point)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AssertionError, TimeoutError, serial.SerialException) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
