# Addressable LED output

One backend is selected by the Kconfig choice and compiled into each build.
All backends provide only pulse timings to the common ESP-IDF RMT transmitter;
the application uses `LedDriver` and never names a protocol. RGB bytes are sent
in GRB order. The named timings are nominal starting points; check the exact
LED datasheet and verify with hardware before relying on them. The generic
backend exposes four pulse widths and the latch delay through Kconfig.

The data pin defaults to -1 (disabled). LED count is limited to 256 and output
is scaled by a configurable brightness limit (default 64/255). This is a signal
limit, not a power supply or current regulator. Use a suitable supply, common
ground, and level shifting if the LEDs require it. `show()` and the diagnostics
are synchronous. The optional boot diagnostic shows red, green, blue and a
moving white pixel, then clears the strip. It is off by default.

For the path from received frame text to `LedDriver::show()`, see
[How a light sequence reaches the LEDs](../../docs/SEQUENCE_CODE_WALKTHROUGH.md).
