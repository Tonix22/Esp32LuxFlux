"""TCP server smoke tests and optional live LuxFlux mDNS inspection."""
import argparse
import pathlib
import socket
import sys
import threading

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "tools"))
from luxflux_tcp_test_server import Lines, serve
from luxflux_mdns import audit, discover, first_available


def test_mode(mode):
    server, client = socket.socketpair()
    server.settimeout(5)
    client.settimeout(5)
    errors = []

    def run_server():
        try:
            with server:
                serve(server, mode, 8)
        except Exception as exc:
            errors.append(exc)

    worker = threading.Thread(target=run_server)
    worker.start()
    with client:
        lines = Lines(client)
        client.sendall(b"SYNC\r\n\0")
        assert lines.read() == "READY TO SYNC"
        client.sendall(b"ACK\nSEQ default\n")
        first = lines.read()
        if mode == "malformed":
            assert first == "1(256,0,0),40"
            client.sendall(b"NACK\n")
        else:
            assert first == "4(255,0,0),4(0,255,0),1000"
            client.sendall(b"ACK\n")
            assert lines.read() == "4(0,255,0),4(255,0,0),1000"
            client.sendall(b"ACK\n")
            assert lines.read() == "EOF"
    worker.join(timeout=5)
    assert not worker.is_alive()
    assert not errors, errors


def probe_server(device, sequence):
    """Probe stored-sequence servers; upload receivers need a JSON payload."""
    if device.role == "upload_server":
        return "Upload receiver: use tools/luxflux_json_server.py with a JSON sequence"
    if device.role != "server" or not device.port or not device.addresses:
        return "TCP probe skipped (device does not advertise a reachable server)"
    with socket.create_connection((device.addresses[0], device.port), timeout=5) as client:
        client.settimeout(5)
        lines = Lines(client)
        client.sendall(b"SYNC\n")
        assert lines.read() == "READY TO SYNC"
        client.sendall(f"ACK\nSEQ {sequence}\n".encode("ascii"))
        frames = 0
        while True:
            record = lines.read()
            if record == "EOF":
                return f"TCP protocol passed ({frames} frames)"
            if record == "NACK":
                raise AssertionError(f"sequence {sequence!r} was rejected")
            assert frames < 256, "too many frames without EOF"
            frames += 1
            client.sendall(b"ACK\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--discover", action="store_true", help="inspect live mDNS devices")
    parser.add_argument("--connect", action="store_true", help="probe discovered server-role ESP32s")
    parser.add_argument("--duration", type=float, default=5.0, help="mDNS browse duration in seconds")
    parser.add_argument("--sequence", default="default", help="sequence for optional TCP probe")
    args = parser.parse_args()
    for selected in ("normal", "fragmented", "coalesced", "malformed"):
        test_mode(selected)
    print("TCP test server modes passed")
    if args.discover:
        devices = discover(args.duration)
        for device in sorted(devices.values(), key=lambda d: (d.logical_name, d.device_id)):
            print(f"{device.device_id}: {device.logical_name or '-'} "
                  f"state={device.state} role={device.role} "
                  f"address={','.join(device.addresses) or '-'}:{device.port}")
            if args.connect:
                print("  " + probe_server(device, args.sequence))
        errors = audit(devices.values())
        for error in errors:
            print("ERROR: " + error)
        print(f"Discovered {len(devices)} device(s); next free name: "
              f"{first_available(devices.values()) or 'none'}")
        if errors:
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
