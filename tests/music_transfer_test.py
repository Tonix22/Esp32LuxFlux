"""Python TCP + audio tests: playback follows EOF and never follows NACK."""
import pathlib
import socket
import sys
import threading
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "tools"))
from luxflux_json_server import serve_with_audio
from luxflux_tcp_test_server import Lines


def check_transfer(reject=False):
    server, client = socket.socketpair()
    server.settimeout(2)
    client.settimeout(2)
    eof_received = threading.Event()
    errors = []

    def receive():
        try:
            with client:
                lines = Lines(client)
                client.sendall(b"SYNC\n")
                assert lines.read() == "READY TO SYNC"
                client.sendall(b"ACK\nSEQ default\n")
                assert lines.read() == "9(0,0,255),1000"
                client.sendall(b"NACK\n" if reject else b"ACK\n")
                if not reject:
                    assert lines.read() == "EOF"
                    eof_received.set()
                else:
                    assert client.recv(32) == b""
        except Exception as exc:
            errors.append(exc)

    def player(command, check):
        assert eof_received.wait(2), "Audio launched before the completed sequence's EOF"
        assert command[0] == "ffplay" and command[-1] == "excerpt.wav" and check

    worker = threading.Thread(target=receive)
    worker.start()
    with patch("luxflux_json_server.subprocess.run", side_effect=player) as playback:
        with server:
            if reject:
                try:
                    serve_with_audio(server, "default", [b"9(0,0,255),1000\n"], "excerpt.wav", 0)
                    raise AssertionError("Rejected transfer accepted")
                except ValueError:
                    pass
            else:
                serve_with_audio(server, "default", [b"9(0,0,255),1000\n"], "excerpt.wav", 0)
        assert playback.call_count == (0 if reject else 1)
    worker.join(2)
    assert not worker.is_alive() and not errors, errors


check_transfer()
check_transfer(reject=True)
print("Music transfer tests passed: EOF before audio, no playback after NACK")
