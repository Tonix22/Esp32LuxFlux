# Configuration ownership

Board pin assignments live in this component's Kconfig and are exposed through
`board_config.h`. Select them with `idf.py menuconfig`; unselected pins remain
-1 (unassigned), while the current non-secret test profile selects GPIO4 for
LED DATA. Confirm board wiring and ESP32-C3 pin restrictions before connecting
hardware. Protocol selection and LED timings belong to `led_driver/Kconfig`.
`sdkconfig.defaults` fixes the target and the non-secret ESP32-C3 test profile.
`sync_config.cpp` converts the generated `CONFIG_*` values into the
`SyncLimits` object passed to the synchronization service. The portable
`SyncLimits` type and its fallback defaults live in `sync_core`; the firmware
version and hardware-independent constants live in `common_config.h`. For the current
synchronization prototype, credentials are entered through menuconfig into
the ignored local `sdkconfig`. Future provisioning can move them to NVS. They
do not belong in source or `sdkconfig.defaults`.

See [How a light sequence reaches the LEDs](../../docs/SEQUENCE_CODE_WALKTHROUGH.md)
for the full route from configuration to the Python server and LED output.
