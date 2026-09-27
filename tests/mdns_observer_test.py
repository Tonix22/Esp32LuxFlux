"""Host-only checks for mDNS TXT parsing and membership audits."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "tools"))
from luxflux_mdns import Device, audit, first_available, parse_service, validate_join


class FakeService:
    name = "luxflux-001122334401._luxflux._tcp.local."
    port = 3333
    properties = {
        b"device_id": b"001122334401", b"logical_name": b"archimedes",
        b"state": b"active", b"role": b"server"
    }

    def parsed_addresses(self):
        return ["192.168.4.1"]


a = parse_service(FakeService())
assert a.device_id == "001122334401"
assert a.addresses == ("192.168.4.1",)
assert a.port == 3333 and a.role == "server"
b = Device("001122334402", "bohr", "active", "client", (), 0, "B")
c = Device("001122334403", "curie", "active", "client", (), 0, "C")
assert audit([a, b, c]) == []
assert first_available([a, c]) == "bohr"
assert validate_join({a.device_id: a, c.device_id: c},
                     {a.device_id: a, b.device_id: b, c.device_id: c}, b.device_id) is None
assert audit([a, Device(b.device_id, "archimedes", "active", "client", (), 0, "B")])
invalid = FakeService()
invalid.properties = {**FakeService.properties, b"device_id": b"not-a-mac"}
assert parse_service(invalid) is None
print("mDNS observer tests passed")
