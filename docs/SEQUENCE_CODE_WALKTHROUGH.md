# How a light sequence reaches the LEDs

This guide follows the current test setup: a Python server on the PC sends a
sequence to the ESP32-C3 client, which controls 9 WS2812B LEDs on GPIO4.
For commands and an explanation of Python's `--mode` choices, see
[Python light-sequence server](PYTHON_SEQUENCE_SERVER.md).

## The path through the code

```text
Python server -> TCP socket -> complete text lines -> validated frames
              -> committed sequence -> effects task -> LED driver -> GPIO4
```

| Step | Code | Job |
| --- | --- | --- |
| 1. Choose settings | [`sdkconfig.defaults`](../sdkconfig.defaults), [`sync_service/Kconfig`](../components/sync_service/Kconfig), [`led_driver/Kconfig`](../components/led_driver/Kconfig), [`sync_config.cpp`](../components/config/sync_config.cpp) | Define the board's role, LED type and count, and transfer limits; convert them to a `SyncLimits` object. |
| 2. Start the application | [`application.cpp`](../components/application/application.cpp) | Initializes the LED driver, effects task, Wi-Fi, and client or server role. |
| 3. Send a sequence | [`luxflux_tcp_test_server.py`](../tools/luxflux_tcp_test_server.py) | The PC replies to the ESP32 handshake and sends text frames. |
| 4. Receive bytes | [`sync_service.cpp`](../components/sync_service/sync_service.cpp) | The ESP32 connects to the PC and reads raw TCP bytes. |
| 5. Assemble lines | [`line_stream.cpp`](../components/sync_core/line_stream.cpp) | Collects bytes until a newline completes a message. |
| 6. Validate and commit | [`sync_protocol.cpp`](../components/sync_core/sync_protocol.cpp), [`legacy_parser.cpp`](../components/sync_core/legacy_parser.cpp) | Checks each frame, acknowledges it, and installs the sequence after `EOF`. |
| 7. Play frames | [`effects_engine.cpp`](../components/effects/effects_engine.cpp) | Expands color groups into individual pixels and repeats frames for their durations. |
| 8. Send LED signal | [`led_driver.cpp`](../components/led_driver/led_driver.cpp) | Applies brightness, packs GRB bytes, and sends timed pulses through RMT. |

## 1. Where the settings come from

ESP-IDF's `Kconfig` files define the configuration menus and their defaults.
The project's [`sdkconfig.defaults`](../sdkconfig.defaults) supplies non-secret
defaults for this board, including WS2812B, 9 LEDs, GPIO4, and Station client
mode. ESP-IDF writes the selected values to the **local, Git-ignored**
`sdkconfig`; Wi-Fi credentials belong there. When building, ESP-IDF makes
those values available to C++ through `sdkconfig.h` as names such as
`CONFIG_LUXFLUX_LED_COUNT`.

In [`sync_config.cpp`](../components/config/sync_config.cpp),
`config::makeLimits()` takes those configured values and puts them into one
`SyncLimits` object:

```cpp
SyncLimits makeLimits()
{
    SyncLimits limits; // Starts with the defaults from rgb_sequence.h.
    limits.led_count = CONFIG_LUXFLUX_LED_COUNT; // Replaces 16 with 9 here.
    // The function sets the remaining fields in the same way.
    return limits;
}
```

`SyncLimits` is a **struct type**; `limits` is one object of that type.
`limits.led_count = ...` changes that object's field. This function is
declared in `sync_config.h` and defined in `sync_config.cpp`, so
`application.cpp` can call it as `config::makeLimits()`. It is not `static`:
a file-local `static` function could not be called from `application.cpp`.
The `Application` constructor passes the returned object to `SyncService`.

The defaults in [`rgb_sequence.h`](../components/sync_core/include/rgb_sequence.h)
let the portable C++ parser and host tests create a `SyncLimits` object without
ESP-IDF. The running firmware uses the values assigned by `makeLimits()`.
For this build, a received frame must describe **exactly 9 LEDs**. The
`max_groups_per_frame` setting counts entries such as `4(255,0,0)`; it is not
the number of LEDs.

## 2. Python server and ESP32 client

The Python server runs on the PC. In the current firmware, `Application`
calls `sync_.startClient(...)`, which starts the ESP32 Wi-Fi Station and a
sync task. That task waits for an IP address, connects to the PC on TCP port
3333, and calls `SyncProtocol::receive()`.

The ESP32 also has a different **SoftAP server** option. When selected in
Kconfig, it calls `sync_.startServer(...)` and serves a built-in demo sequence.
This is a separate role from the Python server used in the current test.

The client and Python server exchange text messages in this order:

```text
ESP32  -> Python: SYNC
Python -> ESP32:  READY TO SYNC
ESP32  -> Python: ACK
ESP32  -> Python: SEQ default
Python -> ESP32:  4(255,0,0),5(0,255,0),40
ESP32  -> Python: ACK
Python -> ESP32:  4(0,255,0),5(255,0,0),60
ESP32  -> Python: ACK
Python -> ESP32:  EOF
```

Each message ends with a newline. `4(255,0,0)` means four red LEDs;
`5(0,255,0)` means five green LEDs. The final number is the frame duration
in milliseconds. The second frame swaps the colors.

## 3. Why TCP chunks do not equal frames

`SocketStream::receive()` in `sync_service.cpp` calls `recv()`. It might get
one character, half a frame, or several complete messages at once. The Python
server's `fragmented` mode deliberately sends bytes separately to test this.

`LineStream::feed()` in `line_stream.cpp` appends incoming bytes to `partial_`.
When it sees a newline, it moves the complete message to `ready_`. The
protocol reads from `ready_`, so `4(255,0,0),5(0,255,0),40` is parsed only
after the whole line has arrived. TCP is allowed to combine or split the
Python writes in any way.

## 4. When the sequence becomes active

`SyncProtocol::receive()` parses each line with `parseLegacyFrame()`. The
parser checks RGB values, frame duration, group count, and that the group
counts add up to the configured LED count. A good frame is kept in a
temporary `staged` sequence and gets an `ACK`.

`EOF` ends the transfer. Only after the **whole sequence** passes validation
does `SequenceStore::replace()` install it. The service then calls
`EffectsEngine::activate()` and sends a `SequenceActivated` event to the
effects task. If a frame is invalid, the ESP32 sends `NACK`; the old active
sequence remains available.

## 5. How pixels reach GPIO4

`EffectsEngine::render()` expands each group into a pixel array. For example,
`9(255,0,0)` becomes nine red `Rgb` values. `taskLoop()` sends the current
frame to `LedDriver::show()`, waits for its duration, advances to the next
frame, and loops back after the last frame. Status colors may appear briefly
during a transfer; the received sequence starts after activation.

`LedDriver::show()` scales colors by the configured brightness limit (64/255
in this test), packs each pixel in GRB wire order, then uses ESP-IDF RMT to
generate the timed LED signal on GPIO4. The chosen WS2812B backend supplies
the pulse timings through `led_driver_factory.cpp`. A successful protocol log
proves that the driver was called; seeing the strip light also requires
correct wiring and power.

## Where to make a change

| You want to change... | Look here |
| --- | --- |
| LED count, GPIO, protocol, brightness, client/server role | ESP-IDF `menuconfig` and the corresponding `Kconfig` entries; keep non-secret defaults in `sdkconfig.defaults`. `components/config/sync_config.cpp` converts the configured sync limits for C++. |
| Colors or durations sent by the PC | `frame_for()` or `serve()` in `tools/luxflux_tcp_test_server.py`. |
| The text handshake or frame validation | `components/sync_core/sync_protocol.cpp` and `legacy_parser.cpp`. |
| How long frames display or how they loop | `EffectsEngine::taskLoop()` in `components/effects/effects_engine.cpp`. |
| Pixel byte order or the physical LED signal | `components/led_driver/led_driver.cpp` and the selected protocol timing file. |

After changing C++ or ESP-IDF configuration, rebuild and flash the ESP32.
Changing only the Python server's frames needs no firmware rebuild.
