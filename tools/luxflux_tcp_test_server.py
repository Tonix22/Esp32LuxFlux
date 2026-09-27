#!/usr/bin/env python3
"""One-shot LuxFlux RGB TCP test server (Python standard library only)."""

import argparse
import logging
import socket
import time


class Lines:
    def __init__(self, connection, maximum=4096):
        self.connection = connection
        self.pending = bytearray()
        self.maximum = maximum

    def read(self):
        # A TCP recv may contain only part of a line or several lines. Save
        # incomplete bytes until a newline finishes the next protocol message.
        while True:
            indexes = [i for i, byte in enumerate(self.pending) if byte in (0, 10)]
            if indexes:
                end = indexes[0]
                raw = bytes(self.pending[:end]).rstrip(b"\r")
                del self.pending[: end + 1]
                if not raw:
                    continue
                line = raw.decode("ascii", errors="strict")
                logging.info("RX %r", line)
                return line
            data = self.connection.recv(256)
            if not data:
                raise ConnectionError("peer disconnected during a frame")
            logging.info("RX chunk %r", data)
            self.pending.extend(data)
            if len(self.pending) > self.maximum:
                raise ValueError("incoming control line exceeds limit")


def send(connection, payload, fragmented=False):
    logging.info("TX %r", payload)
    if fragmented:
        # Stress the ESP32 line reader by writing one byte at a time. TCP may
        # still combine those writes before the ESP32 receives them.
        for byte in payload:
            connection.sendall(bytes((byte,)))
            time.sleep(0.01)
    else:
        connection.sendall(payload)


def frame_for(led_count, duration, flip=False):
    # The counts in every frame must add up to the ESP32's LED count.
    left = led_count // 2
    right = led_count - left
    if left == 0:
        return f"1({0 if flip else 255},{255 if flip else 0},0),{duration}\n".encode()
    first = "0,255,0" if flip else "255,0,0"
    second = "255,0,0" if flip else "0,255,0"
    return f"{left}({first}),{right}({second}),{duration}\n".encode()


def expect(lines, accepted):
    message = lines.read()
    if message not in accepted:
        raise ValueError(f"expected {accepted}, got {message!r}")
    return message


def serve(connection, mode, led_count):
    lines = Lines(connection)
    # The ESP32 initiates the handshake and names the sequence it wants.
    expect(lines, {"SYNC"})
    send(connection, b"READY TO SYNC\n", mode == "fragmented")
    expect(lines, {"ACK"})
    sequence = lines.read()
    if not sequence.startswith("SEQ ") or not sequence[4:]:
        raise ValueError("missing sequence identifier")
    logging.info("Requested sequence %s", sequence)
    if mode == "malformed":
        send(connection, b"1(256,0,0),40\n")
        expect(lines, {"NACK"})
        logging.info("PASS malformed frame rejected with NACK")
        return
    first = frame_for(led_count, 1000)
    second = frame_for(led_count, 1000, flip=True)
    if mode == "coalesced":
        # One write contains three protocol lines. The ESP32 must read each.
        send(connection, first + second + b"EOF\n")
        expect(lines, {"ACK"})
        expect(lines, {"ACK"})
    else:
        send(connection, first, mode == "fragmented")
        expect(lines, {"ACK"})
        send(connection, second, mode == "fragmented")
        expect(lines, {"ACK"})
        send(connection, b"EOF\n", mode == "fragmented")
    logging.info("PASS %s transfer: 2 frames acknowledged; EOF sent", mode)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=3333)
    parser.add_argument("--led-count", type=int, default=8)
    parser.add_argument("--mode", choices=("normal", "fragmented", "coalesced", "malformed"),
                        default="normal")
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--log-file")
    args = parser.parse_args()
    if not 1 <= args.led_count <= 256 or not 1 <= args.port <= 65535 or args.timeout <= 0:
        parser.error("LED count, port or timeout is out of range")
    handlers = [logging.StreamHandler()]
    if args.log_file:
        handlers.append(logging.FileHandler(args.log_file, encoding="utf-8"))
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s",
                        handlers=handlers)
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind((args.host, args.port))
        listener.listen(1)
        listener.settimeout(args.timeout)
        logging.info("Listening on %s:%d mode=%s LED count=%d", args.host, args.port,
                     args.mode, args.led_count)
        try:
            connection, address = listener.accept()
        except socket.timeout:
            logging.error("No ESP32 client connected within %.1f seconds", args.timeout)
            return 1
        with connection:
            connection.settimeout(args.timeout)
            logging.info("Accepted %s:%d", *address)
            serve(connection, args.mode, args.led_count)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
