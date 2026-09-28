#!/usr/bin/env python3
"""Discover a LuxFlux device through mDNS and upload a JSON RGB sequence."""

import argparse
import ipaddress
import json
import logging
import pathlib
import re
import socket
import shutil
import subprocess
import time

from luxflux_mdns import discover
from luxflux_tcp_test_server import Lines, expect, send


def integer(value, minimum, maximum, label):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f"{label} must be an integer from {minimum} to {maximum}")
    return value


def encode_sequence(data, max_frames=128, max_payload=1024, max_duration_ms=10000):
    """Validate JSON before opening a listener; return name and wire records."""
    if not isinstance(data, dict):
        raise ValueError("JSON root must be an object")
    name = data.get("name")
    if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,24}", name):
        raise ValueError("name must contain 1–24 ASCII letters, digits, underscores or hyphens")
    led_count = integer(data.get("led_count"), 1, 256, "led_count")
    frames = data.get("frames")
    if not isinstance(frames, list) or not 1 <= len(frames) <= max_frames:
        raise ValueError(f"frames must contain 1–{max_frames} frames")
    records = []
    for index, frame in enumerate(frames, 1):
        if not isinstance(frame, dict):
            raise ValueError(f"frame {index} must be an object")
        duration = integer(frame.get("duration_ms"), 1, max_duration_ms,
                           f"frame {index} duration_ms")
        groups = frame.get("groups")
        if not isinstance(groups, list) or not 1 <= len(groups) <= led_count:
            raise ValueError(f"frame {index} groups must contain 1–{led_count} groups")
        parts, total = [], 0
        for group in groups:
            if not isinstance(group, dict):
                raise ValueError(f"frame {index} group must be an object")
            count = integer(group.get("count"), 1, led_count, f"frame {index} group count")
            rgb = group.get("rgb")
            if not isinstance(rgb, list) or len(rgb) != 3:
                raise ValueError(f"frame {index} rgb must contain three channel values")
            channels = [integer(channel, 0, 255, f"frame {index} RGB channel") for channel in rgb]
            parts.append(f"{count}({channels[0]},{channels[1]},{channels[2]}),")
            total += count
        if total != led_count:
            raise ValueError(f"frame {index} describes {total} LEDs; expected {led_count}")
        record = ("".join(parts) + str(duration)).encode("ascii")
        if len(record) > max_payload:
            raise ValueError(f"frame {index} exceeds {max_payload} text bytes")
        records.append(record + b"\n")
    return name, records


def select_device(devices, selector=None):
    active = [device for device in devices.values() if device.state == "active"]
    matches = [device for device in active if selector is None or
               device.device_id == selector.upper() or device.logical_name == selector.lower()]
    if len(matches) != 1:
        raise ValueError(f"Expected one active matching device, found {len(matches)}; "
                         "use --device with its factory ID or scientist name")
    device = matches[0]
    if device.role not in {"client", "upload_server"}:
        raise ValueError("Selected device cannot receive uploads; select Station upload "
                         "server in firmware menuconfig (legacy SoftAP servers only serve sequences)")
    if device.role == "upload_server" and not 1 <= device.port <= 65535:
        raise ValueError("Upload server did not advertise a valid TCP port")
    return device


def serve_json(connection, name, records):
    lines = Lines(connection)
    expect(lines, {"SYNC"})
    send(connection, b"READY TO SYNC\n")
    expect(lines, {"ACK"})
    requested = lines.read()
    if requested != "SEQ " + name:
        send(connection, b"NACK\n")
        raise ValueError(f"Device requested {requested!r}; JSON sequence is {name!r}. "
                         "Match the firmware's requested sequence name in menuconfig")
    for index, record in enumerate(records, 1):
        send(connection, record)
        expect(lines, {"ACK"})
        logging.info("Frame %d/%d acknowledged", index, len(records))
    send(connection, b"EOF\n")
    logging.info("All %d frames acknowledged; EOF sent. Check serial logs for commit/save status.",
                 len(records))


def serve_with_audio(connection, name, records, audio=None, delay_ms=250):
    """Play only after the existing TCP transfer has acknowledged all frames."""
    serve_json(connection, name, records)
    if audio is not None:
        time.sleep(delay_ms / 1000)
        logging.info("Starting audio after EOF (approximate synchronization)")
        subprocess.run(["ffplay", "-v", "error", "-nostats", "-nodisp", "-autoexit",
                        str(audio)], check=True)


def connect_device(device, timeout, io_timeout, port=None):
    """Resolve the connection from mDNS; allow the new listener time to bind."""
    addresses = sorted(address for address in device.addresses
                       if ipaddress.ip_address(address).version == 4)
    if not addresses:
        raise ValueError("Selected device has no advertised IPv4 address")
    target_port = device.port if port is None else port
    deadline = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < deadline:
        for address in addresses:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            try:
                connection = socket.create_connection((address, target_port),
                                                      timeout=min(io_timeout, remaining))
            except OSError as exc:
                last_error = exc
                continue
            connection.settimeout(io_timeout)
            logging.info("Connected to %s:%d (%s)", address, target_port, device.device_id)
            return connection
        time.sleep(min(0.25, max(0, deadline - time.monotonic())))
    raise TimeoutError(f"Could not connect to advertised ESP32 upload server: {last_error}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("json_file", type=pathlib.Path)
    parser.add_argument("--device", help="factory MAC ID (recommended) or scientist name; auto-selects a sole device")
    parser.add_argument("--host", default="0.0.0.0", help="listening address for legacy client firmware only")
    parser.add_argument("--port", type=int, help="override mDNS port; legacy listener defaults to 3333")
    parser.add_argument("--discovery-seconds", type=float, default=5.0)
    parser.add_argument("--timeout", type=float, default=60.0, help="wait for selected device (seconds)")
    parser.add_argument("--io-timeout", type=float, default=5.0)
    parser.add_argument("--max-frames", type=int, default=128)
    parser.add_argument("--max-payload", type=int, default=1024)
    parser.add_argument("--max-duration-ms", type=int, default=10000)
    parser.add_argument("--audio", type=pathlib.Path, help="audio file to play once after EOF (requires ffplay)")
    parser.add_argument("--audio-delay-ms", type=int, default=250, help="delay after EOF before launching audio; approximate sync only")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    try:
        if args.port is not None:
            integer(args.port, 1, 65535, "port")
        integer(args.max_frames, 1, 256, "max-frames")
        integer(args.max_payload, 64, 4096, "max-payload")
        integer(args.max_duration_ms, 1, 60000, "max-duration-ms")
        if min(args.timeout, args.io_timeout, args.discovery_seconds) <= 0:
            raise ValueError("timeouts and discovery duration must be positive")
        if args.audio:
            if not args.audio.is_file() or not shutil.which("ffplay"):
                raise ValueError("Audio playback requires an existing audio file and ffplay")
            if args.audio_delay_ms < 0:
                raise ValueError("audio-delay-ms must be nonnegative")
        data = json.loads(args.json_file.read_text(encoding="utf-8"))
        name, records = encode_sequence(data, args.max_frames, args.max_payload, args.max_duration_ms)
        device = select_device(discover(args.discovery_seconds), args.device)
        addresses = {address for address in device.addresses
                     if ipaddress.ip_address(address).version == 4}
        if not addresses:
            raise ValueError("Selected device has no advertised IPv4 address")
        logging.info("Selected %s (%s) at %s", device.device_id, device.logical_name,
                     ", ".join(sorted(addresses)))
        if device.role == "upload_server":
            with connect_device(device, args.timeout, args.io_timeout, args.port) as connection:
                serve_with_audio(connection, name, records, args.audio, args.audio_delay_ms)
            return 0
        logging.warning("Legacy Station client firmware: ESP32 must know this computer's IP. "
                        "Select Station upload server in menuconfig to use inbound mDNS uploads.")
        port = args.port if args.port is not None else 3333
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind((args.host, port))
            listener.listen(4)
            deadline = time.monotonic() + args.timeout
            logging.info("Listening on %s:%d; ESP32 server-host setting must point to this computer",
                         args.host, port)
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("Selected ESP32 did not connect before timeout")
                listener.settimeout(remaining)
                connection, address = listener.accept()
                with connection:
                    if address[0] not in addresses:
                        logging.info("Ignoring connection from unselected address %s", address[0])
                        continue
                    connection.settimeout(args.io_timeout)
                    logging.info("Selected device connected from %s", address[0])
                    serve_with_audio(connection, name, records, args.audio, args.audio_delay_ms)
                    return 0
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
        logging.error("%s", exc)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
