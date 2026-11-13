"""Verify repeated C3 PING/PONG processing using a real board's serial log."""

import argparse
import time

import serial


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="C3 serial port, for example COM8")
    parser.add_argument("--count", type=int, default=3, help="Required complete exchanges")
    parser.add_argument("--timeout", type=float, default=30, help="Timeout in seconds")
    args = parser.parse_args()
    if args.count < 1 or args.timeout <= 0:
        parser.error("--count and --timeout must be positive")

    # Set the modem lines before opening to avoid an intentional board reset.
    port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    port.dtr = False
    port.rts = False
    port.port = args.port
    completed = 0
    awaiting_reply = False

    try:
        port.open()
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            line = port.readline().decode("utf-8", errors="replace").strip()
            if not line:
                continue
            print(line, flush=True)

            if any(text in line for text in ("Guru Meditation", "abort() was called", "assert failed")):
                print("FAIL: firmware panic detected")
                return 1
            if "ble_controller: Disconnected" in line:
                awaiting_reply = False
            elif "ble_controller: RX: PING" in line:
                awaiting_reply = True
            elif "ble_controller: TX: PONG queued" in line and awaiting_reply:
                completed += 1
                awaiting_reply = False
                if completed >= args.count:
                    print(f"PASS: {completed} PING/PONG requests processed on C3")
                    return 0
    except serial.SerialException as error:
        print(f"FAIL: {error}")
        return 1
    finally:
        port.close()

    print(f"FAIL: only {completed}/{args.count} exchanges before timeout")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
