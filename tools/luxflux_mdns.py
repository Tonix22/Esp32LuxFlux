"""Observe LuxFlux mDNS identities without changing the firmware TCP roles.

Install the optional host dependency with ``python3 -m pip install zeroconf``.
"""

from dataclasses import dataclass
import re
import threading
import time

SERVICE_TYPE = "_luxflux._tcp.local."
SCIENTISTS = (
    "archimedes", "bohr", "curie", "dirac", "einstein", "faraday",
    "galileo", "hawking", "ibn-al-haytham", "joule", "kepler",
    "lovelace", "maxwell", "newton", "oppenheimer", "planck",
    "quetelet", "rutherford", "schrodinger", "tesla", "ulam",
    "volta", "watt", "xie", "yukawa", "zwicky",
)


@dataclass(frozen=True)
class Device:
    device_id: str
    logical_name: str
    state: str
    role: str
    addresses: tuple[str, ...]
    port: int
    instance: str


def parse_service(info):
    """Return a validated device from a zeroconf ServiceInfo-like object."""
    def field(key):
        value = info.properties.get(key.encode(), b"")
        return value.decode("ascii") if isinstance(value, bytes) else str(value)

    device_id = field("device_id")
    if not re.fullmatch(r"[0-9A-F]{12}", device_id):
        return None
    return Device(device_id, field("logical_name"), field("state"),
                  field("role"), tuple(info.parsed_addresses()),
                  info.port, info.name)


def discover(duration=5.0):
    """Browse, resolve and return devices keyed by permanent factory ID."""
    try:
        from zeroconf import ServiceBrowser, ServiceListener, Zeroconf
    except ImportError as exc:
        raise RuntimeError("mDNS discovery requires: python3 -m pip install zeroconf") from exc

    names = set()
    lock = threading.Lock()

    class Listener(ServiceListener):
        def add_service(self, zc, type_, name):
            with lock:
                names.add(name)

        def update_service(self, zc, type_, name):
            self.add_service(zc, type_, name)

        def remove_service(self, zc, type_, name):
            with lock:
                names.discard(name)

    zc = Zeroconf()
    browser = ServiceBrowser(zc, SERVICE_TYPE, Listener())
    try:
        time.sleep(duration)
        with lock:
            found_names = sorted(names)
        found = {}
        for name in found_names:
            info = zc.get_service_info(SERVICE_TYPE, name, timeout=2000)
            device = parse_service(info) if info else None
            if device:
                found[device.device_id] = device
        return found
    finally:
        browser.cancel()
        zc.close()


def first_available(devices):
    occupied = {device.logical_name for device in devices if
                device.state in ("active", "claiming")}
    return next((name for name in SCIENTISTS if name not in occupied), None)


def audit(devices):
    """Report invalid and duplicate active assignments; gaps alone are valid."""
    errors = []
    owners = {}
    for device in devices:
        if device.state != "active":
            continue
        if device.logical_name not in SCIENTISTS:
            errors.append(f"{device.device_id}: invalid scientist {device.logical_name!r}")
        if device.logical_name in owners:
            errors.append(f"duplicate {device.logical_name}: "
                          f"{owners[device.logical_name]} and {device.device_id}")
        else:
            owners[device.logical_name] = device.device_id
    return errors


def validate_join(before, after, joined_id):
    """Check a confirmed new claim against the earlier observed membership."""
    expected = first_available(before.values())
    joined = after.get(joined_id)
    if joined is None or joined.state != "active":
        return f"{joined_id}: no confirmed assignment in later snapshot"
    if joined.logical_name != expected:
        return f"{joined_id}: expected {expected}, got {joined.logical_name}"
    return None
