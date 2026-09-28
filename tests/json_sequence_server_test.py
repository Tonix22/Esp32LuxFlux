"""Host-only JSON validation, mDNS selection, and TCP transfer tests."""
import copy
import pathlib
import socket
import sys
import threading

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "tools"))
from luxflux_json_server import connect_device, encode_sequence, select_device, serve_json
from luxflux_mdns import Device
from luxflux_tcp_test_server import Lines

data = {"name": "default", "led_count": 9, "frames": [
    {"duration_ms": 1000, "groups": [{"count": 9, "rgb": [255, 0, 0]}]},
    {"duration_ms": 40, "groups": [{"count": 4, "rgb": [0, 255, 0]},
                                    {"count": 5, "rgb": [0, 0, 255]}]}
]}
name, records = encode_sequence(data)
assert records == [b"9(255,0,0),1000\n", b"4(0,255,0),5(0,0,255),40\n"]
for field, value in (("duration_ms", 10001), ("duration_ms", True), ("groups", [])):
    invalid = copy.deepcopy(data)
    invalid["frames"][0][field] = value
    try:
        encode_sequence(invalid)
        raise AssertionError("invalid frame accepted")
    except ValueError:
        pass
invalid = copy.deepcopy(data)
invalid["frames"][0]["groups"][0]["count"] = 8
try:
    encode_sequence(invalid)
    raise AssertionError("wrong LED count accepted")
except ValueError:
    pass
a = Device("001122334401", "archimedes", "active", "client", ("192.168.1.2",), 0, "A")
b = Device("001122334402", "bohr", "active", "client", ("192.168.1.3",), 0, "B")
assert select_device({a.device_id: a}, None) == a
assert select_device({a.device_id: a, b.device_id: b}, a.device_id) == a
assert select_device({a.device_id: a, b.device_id: b}, "bohr") == b
try:
    select_device({a.device_id: a, b.device_id: b})
    raise AssertionError("ambiguous target accepted")
except ValueError:
    pass


def transfer(request="default", reject=False):
    server, client = socket.socketpair()
    server.settimeout(2)
    client.settimeout(2)
    errors = []

    def worker():
        with server:
            try:
                serve_json(server, name, records)
            except ValueError as exc:
                errors.append(exc)

    thread = threading.Thread(target=worker)
    thread.start()
    with client:
        lines = Lines(client)
        client.sendall(b"SYNC\n")
        assert lines.read() == "READY TO SYNC"
        client.sendall(f"ACK\nSEQ {request}\n".encode())
        if request != name:
            assert lines.read() == "NACK"
        else:
            for record in records:
                assert lines.read() == record.decode().strip()
                client.sendall(b"NACK\n" if reject else b"ACK\n")
                if reject:
                    break
            if not reject:
                assert lines.read() == "EOF"
        if reject or request != name:
            assert client.recv(64) == b""  # EOF must not be sent after rejection.
    thread.join(2)
    assert not thread.is_alive()
    assert bool(errors) == (reject or request != name)


transfer()
transfer("different")
transfer(reject=True)

# The ESP32 is now the listener, but still uses the receiver handshake. Test
# an actual outbound Python connection using the address and port from mDNS.
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    listener.settimeout(2)
    device = Device("001122334401", "archimedes", "active", "upload_server",
                    ("127.0.0.1",), listener.getsockname()[1], "A")
    assert select_device({device.device_id: device}) == device
    errors = []

    def receive_upload():
        try:
            connection, _ = listener.accept()
            with connection:
                connection.settimeout(2)
                lines = Lines(connection)
                connection.sendall(b"SYNC\n")
                assert lines.read() == "READY TO SYNC"
                connection.sendall(b"ACK\nSEQ default\n")
                for record in records:
                    assert lines.read() == record.decode().strip()
                    connection.sendall(b"ACK\n")
                assert lines.read() == "EOF"
        except Exception as exc:
            errors.append(exc)

    worker = threading.Thread(target=receive_upload)
    worker.start()
    with connect_device(device, 2, 2) as connection:
        serve_json(connection, name, records)
    worker.join(2)
    assert not worker.is_alive() and not errors, errors

for role, port in (("server", 3333), ("upload_server", 0)):
    invalid = Device(a.device_id, a.logical_name, "active", role, a.addresses, port, "A")
    try:
        select_device({invalid.device_id: invalid})
        raise AssertionError("Unsupported upload target accepted")
    except ValueError:
        pass
print("JSON sequence server tests passed")
