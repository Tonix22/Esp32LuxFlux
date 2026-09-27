# Build, compile and test LuxFlux on ESP32-C3

This project uses the official `espressif/idf:v5.5.4` image and targets
`esp32c3`. Run commands from the repository root. Docker builds do not need
USB access or privileged mode; build files are written to the ignored `build/`
and `build-<backend>/` directories.

## Build the development image and firmware

```bash
docker compose build
docker compose run --rm esp-idf idf.py --version
docker compose run --rm esp-idf idf.py set-target esp32c3
docker compose run --rm esp-idf idf.py build
docker compose run --rm esp-idf bash
```

Run `set-target` when creating or deliberately changing a target. It resets
the generated `sdkconfig`, so use the normal build command after you have
entered local Wi-Fi settings. The equivalent helpers are:

```bash
./scripts/docker-build.sh
./scripts/docker-idf-build.sh
./scripts/docker-shell.sh
```

The firmware helper sets the target on first use and preserves an existing
ESP32-C3 `sdkconfig`. A native build, with ESP-IDF 5.5.4 installed, is:

```bash
. "$IDF_PATH/export.sh"
idf.py set-target esp32c3
idf.py build
```

## Compile all LED drivers

```bash
python3 tools/build_led_variants.py
```

This builds WS2812B, SK6812 RGB, WS2811 at 800 kHz, WS2811 at 400 kHz,
UCS1903, SM16703 and generic RMT in separate directories. One protocol
backend is selected per build. To build just one variant:

```bash
python3 tools/build_led_variants.py --variant sk6812
```

With native ESP-IDF, add `--native`. Compilation is not a physical timing or
color-order test; record those checks in `LED_PROTOCOL_TEST_MATRIX.md`.

## Run host protocol tests

```bash
g++ -std=c++17 -Wall -Wextra -Werror -Icomponents/sync_core/include \
    tests/sync_core_test.cpp components/sync_core/legacy_parser.cpp \
    components/sync_core/line_stream.cpp components/sync_core/sync_protocol.cpp \
    -o /tmp/luxflux-sync-core-test
/tmp/luxflux-sync-core-test
python3 tests/tcp_test_server_test.py
```

These tests cover the exact legacy frame `4(255,0,0),4(0,255,0),40`, one and
multiple RGB groups, invalid colors/counts/duration, length and memory limits,
split and coalesced TCP lines, timeout/retry, disconnect, ACK/NACK, EOF,
preservation after failure and activation after a valid transfer.

## Confirm and flash a connected ESP32-C3

On Linux, inspect `/dev/serial/by-id/` and identify the board's actual serial
device. Set the variable to that observed port; the line below is an example
for a machine where the board appears at `/dev/ttyACM0`:

```bash
SERIAL_PORT=/dev/ttyACM0
docker run --rm --device="$SERIAL_PORT" \
    -v "$PWD:/project" -w /project luxflux-idf:v5.5.4 \
    esptool.py --chip auto -p "$SERIAL_PORT" chip_id
```

Continue only when that command reports `ESP32-C3` for the intended board.
Then flash and monitor with the same explicit port:

```bash
docker run --rm --device="$SERIAL_PORT" \
    -v "$PWD:/project" -w /project luxflux-idf:v5.5.4 \
    idf.py -p "$SERIAL_PORT" flash
docker run --rm -it --device="$SERIAL_PORT" \
    -v "$PWD:/project" -w /project luxflux-idf:v5.5.4 \
    idf.py -p "$SERIAL_PORT" monitor
```

Quit the monitor with `Ctrl+]`. This Docker Compose installation does not
accept `run --device`, so these hardware commands use `docker run`. macOS
Docker Desktop generally does not forward `/dev/cu.*` serial devices directly;
use native ESP-IDF or a remote Linux host. Windows COM ports likewise need
native ESP-IDF or configured WSL2 USB forwarding. Neither operating system
uses the Linux path in the example.

## Configure a live RGB synchronization test

For a beginner-friendly explanation of the Python server, its four modes,
and the 9-LED frame format, see [Python light-sequence server](PYTHON_SEQUENCE_SERVER.md).

The current test profile selects a 9-LED WS2812B 800 kHz strip on GPIO4 and
Station client role. See [Configuration](CONFIGURATION.md) for the exact
setting locations and the distinction between tracked defaults and ignored
local Wi-Fi credentials.

An ESP32-C3 client needs Wi-Fi access to a test server reachable from the
board. First enter the following in the ESP-IDF configuration UI:

```bash
docker compose run --rm esp-idf idf.py menuconfig
```

Under the LuxFlux menus, select Station client role, enter the test network's
SSID/password, set the LED count and sequence name, and optionally set a server
address override when the Python host is not the Wi-Fi gateway. The normal
client path uses the gateway received from DHCP. The credentials remain in the
ignored local `sdkconfig`; do not copy them into source or
`sdkconfig.defaults`. For physical LED checks, select the actual DATA GPIO,
protocol and conservative brightness limit only after confirming board wiring
and LED power. The default GPIO of -1 keeps output disabled.

Rebuild and flash the configured firmware, then keep the serial monitor open.
In another terminal on the reachable host, run one test mode at a time. Match
`--led-count` to the firmware configuration:

```bash
python3 tools/luxflux_tcp_test_server.py --mode normal --led-count 9 \
    --log-file test-results/server-normal.txt
python3 tools/luxflux_tcp_test_server.py --mode fragmented --led-count 9 \
    --log-file test-results/server-fragmented.txt
python3 tools/luxflux_tcp_test_server.py --mode coalesced --led-count 9 \
    --log-file test-results/server-coalesced.txt
python3 tools/luxflux_tcp_test_server.py --mode malformed --led-count 9 \
    --log-file test-results/server-malformed.txt
```

The server listens on `0.0.0.0:3333` by default and has a finite timeout.
Use `--host`, `--port` and `--timeout` when needed; firmware port 3333 is fixed
for this compatibility test. The client retries after a closed connection, so
start the next one-shot server and wait for reconnection.

## What the PC server and ESP32 client must exchange

Run the Python server on the PC first; then power/reset the board and watch
both logs. For a normal two-frame test, the expected order is:

| Step | Sender | Receiver | Expected evidence |
| --- | --- | --- | --- |
| 1 | ESP32 Station | Wi-Fi AP | Serial log says Station obtained an IP; without this, no TCP test has started |
| 2 | ESP32 TCP client | PC server `:3333` | Server log says `Accepted`; serial log says `Connecting to RGB server` |
| 3 | ESP32 | PC | `SYNC` |
| 4 | PC | ESP32 | `READY TO SYNC` |
| 5 | ESP32 | PC | `ACK`, then `SEQ default` |
| 6 | PC | ESP32 | One 9-pixel RGB frame ending in duration, for example `4(255,0,0),5(0,255,0),40` |
| 7 | ESP32 | PC | `ACK` only after parsing and validating that complete frame |
| 8 | PC / ESP32 | Each other | Repeat frame / `ACK` for the second frame |
| 9 | PC | ESP32 | `EOF`; ESP32 validates and commits the staged sequence |
| 10 | ESP32 effects task | Generic LED driver | Serial log says sequence committed and `Complete frame sent to generic LED driver` |

In `fragmented` mode, the PC sends bytes of each line separately. In
`coalesced` mode, it writes two frames and `EOF` together; TCP may still
deliver them in any number of reads. Both modes must produce the same two
frame ACKs and final activation as normal mode. In `malformed` mode, the PC
sends an out-of-range RGB value; the ESP32 must answer `NACK`, report a
validation failure, and keep any earlier valid sequence active. The server
script exits after one connection or a finite timeout. Run the four commands
one at a time, and compare each matching pair of PC and serial logs. Do not
infer protocol success from the Python server starting or a firmware flash.

For each mode, save the corresponding serial output under `test-results/`.
On Linux, `script -q test-results/serial-test.txt` starts a recording shell;
run the monitor there, quit it with `Ctrl+]`, then type `exit` to finish the
recording. Confirm the serial log shows handshake completion, received byte
chunks, decoded groups, LED total, duration, ACK/NACK, EOF and sequence
activation. The Python log must show the matching ACKs or NACK. A coalesced
send does not guarantee a single TCP read, so check what the serial log
actually recorded. A malformed transfer must leave the earlier active sequence
in place. Claim physical light output only after observing a connected strip.

## Current validation record

The Docker image, container startup, ESP-IDF version, explicit ESP32-C3 target
setup, main firmware build, all seven LED variants, host tests, chip query,
flash and serial boot have passed. See `../test-results/` for logs and
`../test-results/LIVE_SYNC_STATUS.md` for the latest live result. A fragmented
9-LED TCP transfer passed on the ESP32-C3 with the WS2812B backend selected.
Physical LED light output has not been visually verified.
