# LuxFlux ESP32-C3 scaffold

Step-by-step commands are in [Build, compile and test](docs/BUILD_COMPILE_TEST.md).
For the Python test server and its modes, see
[Python light-sequence server](docs/PYTHON_SEQUENCE_SERVER.md).
To follow the server data through the C++ client and LED output, see
[How a light sequence reaches the LEDs](docs/SEQUENCE_CODE_WALKTHROUGH.md).

ESP-IDF 5.5.4 project for an ESP32-C3 spinning-top light controller. The
firmware contains a selectable LED RMT backend and a modular RGB sequence
synchronization path. Motion, RPM, audio, battery management and final light
behavior are deferred.

The [NVM component](components/nvm/README.md) saves a complete, validated light
sequence to NVS after `EOF` and restores it on boot. LED configuration storage
remains a placeholder.

## Docker development

The official `espressif/idf:v5.5.4` image is pinned via `IDF_VERSION` in
`Dockerfile` and Compose. Override deliberately with `IDF_VERSION=vX.Y.Z`.
Docker Compose mounts this repository at `/project`; all build artifacts stay
under the repository's ignored `build/` or `build-<variant>/` directories.
No privileged mode or USB mapping is used to compile.

```bash
docker compose build
docker compose run --rm esp-idf idf.py --version
docker compose run --rm esp-idf idf.py set-target esp32c3
docker compose run --rm esp-idf idf.py build
docker compose run --rm esp-idf bash
python3 tools/build_led_variants.py
```

Helpers: `./scripts/docker-build.sh`, `./scripts/docker-shell.sh`, and
`./scripts/docker-idf-build.sh`. The last helper sets the target on first use
and then builds without resetting a configured `sdkconfig`.
The Dev Container reuses the Compose service and opens `/project`.

Native build with an installed ESP-IDF 5.5.4 environment:

```bash
. "$IDF_PATH/export.sh"
idf.py set-target esp32c3
idf.py build
python3 tools/build_led_variants.py --native
```

## Quick command and menuconfig reference

Run these commands from the repository root. On a fresh checkout, build the
Docker image and set the target before opening menuconfig or building; this
creates the local `sdkconfig`. Do not run `set-target` again after entering
Wi-Fi credentials, because it resets that configuration. For the hardware
commands, set `SERIAL_PORT` to the confirmed port of your ESP32-C3.

| Task | Command |
| --- | --- |
| Build Docker image (first time) | `docker compose build` |
| Set ESP32-C3 target (first time) | `docker compose run --rm esp-idf idf.py set-target esp32c3` |
| Build firmware | `docker compose run --rm esp-idf idf.py build` |
| Flash firmware | `docker run --rm --device="$SERIAL_PORT" -v "$PWD:/project" -w /project luxflux-idf:v5.5.4 idf.py -p "$SERIAL_PORT" flash` |
| View serial monitor | `docker run --rm -it --device="$SERIAL_PORT" -v "$PWD:/project" -w /project luxflux-idf:v5.5.4 idf.py -p "$SERIAL_PORT" monitor` |
| Open menuconfig | `docker compose run --rm esp-idf idf.py menuconfig` |

To set `SERIAL_PORT` on Linux, plug in the ESP32-C3 and list the serial devices:

```bash
ls -l /dev/serial/by-id/
```

Find the entry for your board and note the `/dev/ttyACM0` or `/dev/ttyUSB0`
device it points to. Unplugging and reconnecting the board can help identify
which entry appeared. If `/dev/serial/by-id/` does not exist, compare the
results of `ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null` before and after
connecting the board. Set the variable in the same terminal where you will run
the flash or monitor command, using the device you found:

```bash
export SERIAL_PORT=/dev/ttyACM0
ls -l "$SERIAL_PORT"
```

The path above is an example; replace it with your board's actual device.
Confirm that the selected port reports an ESP32-C3 before flashing:

```bash
docker run --rm --device="$SERIAL_PORT" \
    -v "$PWD:/project" -w /project luxflux-idf:v5.5.4 \
    esptool.py --chip auto -p "$SERIAL_PORT" chip_id
```

Exit the serial monitor with `Ctrl+]`. Rebuild and flash after changing
menuconfig settings. The flash and monitor commands above use Linux device
paths; see [Build, compile and test](docs/BUILD_COMPILE_TEST.md) for device
identification and other operating systems.

| Menuconfig menu | Settings you can change |
| --- | --- |
| **LuxFlux board configuration** | LED DATA GPIO; I2C SDA/SCL, IMU interrupt, microphone, battery ADC, and status LED GPIOs. `-1` leaves a pin unassigned or disables LED output. The project defaults set LED DATA to GPIO4. |
| **LuxFlux LED driver** | LED protocol (WS2812B, SK6812 RGB, WS2811 800/400 kHz, UCS1903, SM16703, or generic RMT); LED count (project default: 9); maximum channel brightness; boot diagnostic. The generic protocol also exposes bit and reset timing. |
| **LuxFlux network driver** | Optional scan and log of the target Wi-Fi access point before connecting. |
| **LuxFlux RGB synchronization** | Disabled, SoftAP server, or Station client role (project default: client); Wi-Fi SSID and password; requested sequence name; optional TCP server address override; limits for sequences, frames, groups, payload and memory; frame duration bounds; socket timeout and retry count. |
| **LuxFlux mDNS discovery** | Discovery query timeout, delay between the three discovery attempts, and periodic verification interval. |

Wi-Fi credentials and local menuconfig choices are stored in the ignored
`sdkconfig`; the included `sdkconfig.defaults` provides the project defaults.
See [Configuration](docs/CONFIGURATION.md) for details.

## mDNS scientist identities

Each ESP32 advertises `_luxflux._tcp.local.` with a service instance and
hostname derived from its permanent factory MAC, such as
`luxflux-7C9EBD123456`. Its TXT records contain `device_id`, `logical_name`,
`state`, and TCP `role`. The logical name starts with the first available name
in the ordered scientist registry and is confirmed only after three discovery
attempts and collision verification. A device repeats enumeration after a Wi-Fi
reconnection; an established device keeps its name when another device joins.
The lower factory MAC wins when two devices have competing claims on the same
name. If all 26 names are occupied, the device waits and retries without
starting RGB TCP synchronization.

The existing TCP direction is unchanged: Station clients connect to the
configured host or DHCP gateway, while SoftAP servers accept TCP connections
on port 3333. Client-role mDNS records use port 0 and cannot be probed as TCP
servers. The RGB protocol itself does not carry the scientist name; clients
can observe changes through updated mDNS TXT records and serial logs.

On a computer on the same multicast-capable network, inspect devices with:

```bash
python3 -m pip install -r tests/requirements-mdns.txt
python3 tests/tcp_test_server_test.py --discover
```

Add `--connect` to probe discovered **server-role** ESP32s using the existing
RGB TCP protocol. The default test command still runs locally without
`zeroconf` or hardware. For a host-only enumeration test, run:

```bash
g++ -std=c++17 -Wall -Wextra -Werror \
    -Icomponents/mdns_discovery/include tests/mdns_enumeration_test.cpp \
    components/mdns_discovery/enumeration.cpp -o /tmp/luxflux-mdns-enumeration-test
/tmp/luxflux-mdns-enumeration-test
python3 tests/mdns_observer_test.py
```

mDNS discovery depends on multicast visibility. Network isolation, lost
responses, or a partition can temporarily produce duplicate names. Periodic
verification resolves visible conflicts later, but mDNS cannot guarantee one
globally consistent view across separated networks. A missed periodic query
does not remove an existing assignment.

## Optional flashing

Connect hardware and identify its serial port explicitly. On Linux, a board
may appear as `/dev/ttyACM0` or `/dev/ttyUSB0`; permissions must allow access.
This installation of Docker Compose does not provide `run --device`; pass the
device explicitly with `docker run`. For example, only if your board actually
appears at `/dev/ttyACM0` and has been confirmed as an ESP32-C3:

```bash
docker run --rm -it --device=/dev/ttyACM0 \
    -v "$PWD:/project" -w /project luxflux-idf:v5.5.4 \
    idf.py -p /dev/ttyACM0 flash monitor
```

On macOS, USB serial paths such as `/dev/cu.*` are generally not forwarded
through Docker Desktop; use native ESP-IDF or a remote Linux host for flashing.
On Windows, COM ports are not directly mapped to Linux containers by Docker
Desktop; use native Windows ESP-IDF or WSL2 USB forwarding configured for the
specific device. This workspace's `/dev/ttyACM0` was confirmed as an ESP32-C3,
flashed and observed booting; that path is an example for this host only.

## Configuration and architecture

`idf.py menuconfig` selects board pins, one LED backend, LED count, brightness
limit, optional boot diagnostic and RGB synchronization role. The current
test profile selects GPIO4, 9 LEDs, WS2812B at 800 kHz and Station client
mode; other board pins remain unassigned. See
`docs/CONFIGURATION.md` for values and setting ownership. Put Wi-Fi credentials
only in the ignored local `sdkconfig` via menuconfig; never add them to headers
or `sdkconfig.defaults`. The RGB server starts a SoftAP, binds TCP port 3333
on `0.0.0.0`, and currently serves a two-frame demonstration sequence under
the configured sequence name. The client joins the named Wi-Fi network,
connects to the gateway address supplied by DHCP, and requests that sequence.
An optional server-address override exists only for lab setups where a Python
test server is not the gateway. Role selection is independent of LED protocol.

The transport uses `SYNC`, `READY TO SYNC`, `ACK`, `NACK` and `EOF` lines.
It accepts LF, CRLF and an optional legacy NUL terminator; each frame uses
`count(R,G,B),...,duration_ms`. Per-frame ACKs are required; EOF completes
the transfer. Invalid transfers leave the previous sequence intact. Typed events
reach a separate effects task, which owns status animation and sequence
playback through the generic LED API. Socket reads have a timeout and bounded
retries; TCP disconnects never block LED refresh indefinitely. The effects
task can run without an LED pin, but no physical output occurs until a pin is
configured.

### Sequence and effect limits

A transmitted effect is one named RGB sequence containing one or more frames.
One TCP transfer carries **one sequence**, ending with `EOF`. With the default
configuration, you can send **up to 128 frames per sequence**, provided the
frame text and memory limits below are also satisfied. Send each frame as
`count(R,G,B),...,duration_ms` and wait for its `ACK` before sending the next
frame; after the final frame, send `EOF`.

| Limit | Default | Allowed configuration range |
| --- | --- | --- |
| Frames per sequence | 128 | 1–256 |
| Duration of one frame | 1–10,000 ms | Minimum: 1–1,000 ms; maximum: 1–60,000 ms |
| Text length of one frame | 1,024 bytes | 64–4,096 bytes |
| RGB groups (`count(R,G,B)` entries) per frame | 256 | 1–256; also bounded by LED count and text length |
| Stored named sequences in RAM | 4 | 1–8 |
| Estimated memory for stored sequences plus the incoming sequence | 16,384 bytes (16 KiB) | 1,024–32,768 bytes |
| LEDs described by each frame | Exactly 9 in the project defaults | Match the configured LED count (1–256) |

Change these limits under **LuxFlux RGB synchronization** in `menuconfig`;
change LED count under **LuxFlux LED driver**. Rebuild and flash to apply the
new limits. These values match the current test configuration and Kconfig
defaults; local menuconfig changes can override them.

At the default limits, 128 frames lasting 10,000 ms each give a maximum
**single-cycle duration of 21 minutes 20 seconds**, if the sequence fits the
memory budget. Playback repeats the sequence, so this is not a limit on how
long the effect can keep running. For comparison, 128 frames at 40 ms each
give a 5.12-second cycle.

Every group must describe at least one LED, and the group counts must total
the configured LED count. With 9 LEDs, a frame therefore has at most **9
groups**, even though the configured group limit is 256. A solid-color frame
such as `9(255,0,0),1000` uses only one group.

The text limit applies to each frame line, not the entire sequence. With LF
line endings, the final newline does not count toward that limit; with CRLF,
the carriage return consumes one byte in the receive buffer. The memory
limit accounts for frame structures and RGB groups, rather than the TCP text
size or all heap overhead. An existing sequence remains stored while its
replacement is received, so **both versions count toward the memory budget**.
Consequently, the maximum number of frames that fit depends on group counts
and what is already stored.

The firmware accepts the new sequence only after complete validation at
`EOF`. A transfer that exceeds a limit is rejected and leaves the previous
sequence intact. NVS persistence has its own available storage capacity; a
sequence accepted in RAM can still fail to save if NVS is full. The supplied
Python test server sends only two frames by default; it is not a general
sequence-upload tool.

### Send a sequence from JSON using mDNS

Use [luxflux_json_server.py](tools/luxflux_json_server.py) to select an active
ESP32 by its mDNS identity and send the frames in a JSON file. Install the host
dependency, then try the provided 9-LED example:

```bash
python3 -m pip install -r tests/requirements-mdns.txt
python3 tools/luxflux_json_server.py tools/sequences/example.json --device archimedes
```

For a stable target, pass its full factory ID instead of its scientist name,
for example `--device ACA704D01DC8`. If exactly one active device is discovered,
`--device` can be omitted. Duplicate or ambiguous scientist names are rejected.

The ESP32 must use **Station client** mode and have its server-address override
set to this computer's reachable IPv4 address. mDNS identifies the target;
the existing firmware still initiates the TCP connection to the computer.
The script listens on port 3333, accepts the selected device's advertised IPv4
address, and exits after sending one sequence. The current ESP32 server role
does not accept sequence uploads. Stop other servers using port 3333 first.

The JSON format is:

```json
{
  "name": "default",
  "led_count": 9,
  "frames": [
    {"duration_ms": 1000, "groups": [{"count": 9, "rgb": [255, 0, 0]}]},
    {"duration_ms": 1000, "groups": [{"count": 9, "rgb": [0, 0, 255]}]}
  ]
}
```

`name` must match the firmware's **Sequence name to request** setting.
`led_count` must match the board, and the `count` values in every frame must
total that number. RGB channels must be integers from 0 to 255. The script
validates the file before listening, sends one frame at a time, waits for each
`ACK`, and sends `EOF` only after every frame was acknowledged. The protocol
has no final commit acknowledgement; check serial logs for activation and NVS
save results. Firmware memory and other configured limits can still reject a
frame with `NACK`.

Run `python3 tools/luxflux_json_server.py --help` for discovery/connection
timeouts and frame, payload, and duration limit overrides. Only raise those
limits to match a reconfigured board. Run the host tests with:

```bash
python3 tests/json_sequence_server_test.py
```

For a song-analysis prototype that generates frequency-color frames and beat
pulses, then starts matching audio after `EOF`, see
[Music-to-light demo](docs/MUSIC_LIGHT_DEMO.md). It includes a licensed music
excerpt, its generated JSON, and instructions for analyzing other songs.

Compile and run the portable protocol tests:

```bash
g++ -std=c++17 -Wall -Wextra -Werror -Icomponents/sync_core/include \
    tests/sync_core_test.cpp components/sync_core/legacy_parser.cpp \
    components/sync_core/line_stream.cpp components/sync_core/sync_protocol.cpp \
    -o /tmp/luxflux-sync-core-test
/tmp/luxflux-sync-core-test
```

To emulate the legacy TCP server on a reachable host, select the client role,
set the Wi-Fi credentials and LED count in menuconfig, and run one of:

```bash
python3 tools/luxflux_tcp_test_server.py --mode normal --led-count 9
python3 tools/luxflux_tcp_test_server.py --mode fragmented --led-count 9
python3 tools/luxflux_tcp_test_server.py --mode coalesced --led-count 9
python3 tools/luxflux_tcp_test_server.py --mode malformed --led-count 9
```

The script uses only the Python standard library, listens on port 3333 by
default and exits on timeout. Set `--host`, `--port` or `--log-file` as needed.
If the test host is not the Wi-Fi gateway, set the client address override in
menuconfig to its reachable IPv4 address. The configured LED count must match
the test server argument. The test server is for a controlled local network;
the protocol does not authenticate peers or encrypt the TCP stream.

The original ESP8266 code in `Firmware/drivers/TCP_IP`, `middleware`,
`interfaces`, `HAL` and `Test/ESP_TCP_Test` informed the handshake, named
sequence request, frame format and status events. This implementation fixes
the original one-`recv`-per-message assumption, fixed 128-byte buffers,
hardcoded eight-pixel frames and IP addresses, storage writes before complete
validation, restart after connection, untyped queue messages and indefinite
socket waits. The legacy parser is isolated from the hardware LED component.

## ESP32WOL reference and differences

The read-only `/home/tonixscarlet/Documents/ESP32WOL` reference supplied the
ESP-IDF 5.5 release choice, ESP32-C3 target, conventional top-level CMake
shape, `/project` container workspace, bind-mounted source and optional USB
device workflow. Reviewed `Docker/Dockerfile`, `Docker/ReadMe.md`,
`Docker/InstallDocker.md`, `Docker/installdocker.sh`, `ReadMe.md`, root and main
`CMakeLists.txt`, and the generated `build/config.env` version/target entries.
The Wi-Fi Station initialization in the read-only `main/main.c` was also
compared during hardware diagnosis; its explicit WPA2 threshold and disabled
power saving informed the current driver settings.
It has no Compose or Dev Container configuration. This project uses a pinned
official image instead of cloning a moving release branch, Compose instead of
manual `docker run`, a separate C++ component tree, configurable board pins
and no WOL application logic or copied secrets.
