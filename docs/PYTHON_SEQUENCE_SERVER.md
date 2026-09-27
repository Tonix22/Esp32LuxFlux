# Python light-sequence server

This guide is for `tools/luxflux_tcp_test_server.py`. Run it on your computer;
the ESP32-C3 runs the **client** firmware and connects to the computer. The
server sends a short light sequence to the ESP32 over Wi-Fi. It uses only
Python's standard library, so no Python packages need to be installed.

## Try it with the current 9-LED board

The current test firmware is a Wi-Fi client configured for **9 WS2812B LEDs**,
with LED DATA on **GPIO4**. From the project directory on the computer, run:

```bash
python3 tools/luxflux_tcp_test_server.py --mode normal --led-count 9 --timeout 60
```

You should first see `Listening on 0.0.0.0:3333`. Leave that terminal open.
The ESP32 retries its connection automatically; restarting the board is
optional. When it connects, the server prints `Accepted`, the messages sent
and received, and finally `PASS normal transfer`. The server handles **one
connection and then exits**. Run the command again for another test.

The computer and ESP32 must be reachable on the same local network. The
ESP32's configured server address must be the computer's current IPv4 address;
the last live test used `192.168.100.119`. On Linux, `ip -4 address` shows the
computer's current addresses. `0.0.0.0` in the `Listening` message means the
Python program accepts connections on the computer's network interfaces; it
is **not** the ESP32's address. Port `3333` must match the ESP32 firmware.

To see the lights, the strip also needs power, a shared ground with the board,
and its DATA input connected to GPIO4. The protocol can pass even when the
strip is disconnected. The configured brightness limit is 64 out of 255.

## What the server sends

The ESP32 starts by sending `SYNC`. Python replies `READY TO SYNC`; the ESP32
answers `ACK` and requests `SEQ default`. Python then sends these two frames:

```text
4(255,0,0),5(0,255,0),40
4(0,255,0),5(255,0,0),60
EOF
```

In the first line, `4(255,0,0)` means **4 red LEDs**, `5(0,255,0)` means
**5 green LEDs**, and `40` means show the frame for **40 milliseconds**. The
second line swaps red and green for **60 milliseconds**. Each RGB number runs
from 0 to 255. The counts in each frame must add up to `--led-count 9`.
The ESP32 sends `ACK` after each valid frame. `EOF` tells it the full sequence
has arrived; the ESP32 then starts playing the two frames repeatedly.

## What `--mode` means

`--mode` chooses **how Python sends the test data**, not which LED driver or
colors the ESP32 uses. The three successful modes send the same two frames
above.

| Mode | What Python does | What it tests |
| --- | --- | --- |
| `normal` | Sends each complete line, then waits for its `ACK`. This is the easiest first test and is the default if `--mode` is omitted. | Basic connection, frame parsing, and LED sequence transfer. |
| `fragmented` | Sends each byte separately with a short pause. For example, the characters in a frame are sent one at a time. | The ESP32 can collect pieces until it has a complete line. The lights should show the same sequence as `normal`. |
| `coalesced` | Sends both frames and `EOF` together in one send operation after the handshake. | The ESP32 can process several lines that arrive together. The lights should show the same sequence as `normal`. |
| `malformed` | Sends an invalid color value, `256`, and expects the ESP32 to reply `NACK`. | Rejection of bad data. This mode is supposed to fail the frame; it should not install a new sequence. |

TCP is a stream of bytes. A single Python send does **not** guarantee a single
ESP32 receive call: the network may combine bytes or split them differently.
That is why the ESP32 waits for the end of a line before parsing a frame.
Run only one mode at a time; each command uses the same TCP port:

```bash
python3 tools/luxflux_tcp_test_server.py --mode coalesced --led-count 9 --timeout 60
python3 tools/luxflux_tcp_test_server.py --mode malformed --led-count 9 --timeout 60
```

These are two separate commands. The `malformed` test should print
`PASS malformed frame rejected with NACK`, rather than installing new colors.

To run the byte-by-byte test used in the live WS2812B check:

```bash
python3 tools/luxflux_tcp_test_server.py --mode fragmented --led-count 9 --timeout 60 \
    --log-file test-results/server-fragmented.txt
```

## Make all 9 LEDs red

The script currently has no command-line color option. To make its two frames
solid red, open `tools/luxflux_tcp_test_server.py` and, inside `serve()`,
replace the `first = ...` and `second = ...` lines with:

```python
first = f"{led_count}(255,0,0),1000\n".encode()
second = first
```

Run the `normal` command above again. Both frames will set all 9 LEDs red for
1,000 milliseconds each. After `EOF`, the ESP32 repeats them, so the strip
stays red. The ESP32 may briefly show a status color while the transfer is in
progress. Change only these two lines back to restore the red/green demo.
The original lines are:

```python
first = frame_for(led_count, 40)
second = frame_for(led_count, 60, flip=True)
```

## Other options and common messages

| Option | Meaning |
| --- | --- |
| `--led-count 9` | Build frames containing 9 LEDs. Always pass this for the current firmware; the script's default is 8. |
| `--timeout 60` | Wait up to 60 seconds for a client and use that timeout while reading the connection. The default is 15 seconds. |
| `--log-file PATH` | Save the same server messages to a file as well as showing them in the terminal. |
| `--host ADDRESS` | Listen on a particular **computer** address. Usually leave this at the default `0.0.0.0`. |
| `--port NUMBER` | Listen on another TCP port. Keep the default `3333` unless the ESP32 firmware is changed to match. |

`TX` means Python sent data to the ESP32; `RX` means Python received data from
it. `RX chunk` shows one piece received by Python, and `ACK` means the ESP32
accepted the preceding step. If you see `No ESP32 client connected`, check
that the ESP32 obtained a Wi-Fi IP, that its server address matches the
computer, and that a firewall is not blocking TCP port 3333. A server-side
`PASS` confirms the expected replies; check the ESP32 serial log for `EOF
handled`, `Validated sequence committed`, and LED-driver output to confirm
the client finished its part. Actual light output must be checked by looking
at the connected strip.

The successful fragmented test from this workspace is recorded in
`test-results/server-ws2812b-fragmented.txt` and
`test-results/serial-ws2812b-fragmented.txt`.
