"""Loopback smoke tests for the standard-library TCP test server modes."""
import pathlib
import socket
import sys
import threading

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "tools"))
from luxflux_tcp_test_server import Lines, serve


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
            assert first == "4(255,0,0),4(0,255,0),40"
            client.sendall(b"ACK\n")
            assert lines.read() == "4(0,255,0),4(255,0,0),60"
            client.sendall(b"ACK\n")
            assert lines.read() == "EOF"
    worker.join(timeout=5)
    assert not worker.is_alive()
    assert not errors, errors


for selected in ("normal", "fragmented", "coalesced", "malformed"):
    test_mode(selected)
print("TCP test server modes passed")
