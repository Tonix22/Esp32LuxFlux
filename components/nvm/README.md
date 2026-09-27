# NVM storage

`NvmStore::saveSequence()` stores the active named light sequence as one NVS
blob. The record has a format version and checksum. `loadSequence()` checks
these, parses each frame with the current sync limits, and validates the full
sequence before returning it. Missing or invalid records are never activated.
Only the latest active sequence is retained in NVS. A failed write leaves RAM
playback available, and the sync service logs the write error.

This component also reserves an API for saving and loading LED settings in
non-volatile memory. `LedConfiguration` contains LED count, DATA GPIO, and
brightness limit. These are the settings that a future startup path could
restore before initializing `LedDriver`.

`NvmStore::saveLedConfiguration()` and `loadLedConfiguration()` are placeholders.
Both return `ESP_ERR_NOT_SUPPORTED`; neither reads or writes flash. A failed
load leaves the caller's `LedConfiguration` object unchanged. The application
does not call these LED configuration methods, so the current `sdkconfig`
values remain the source of the running LED setup.

The LED protocol, such as WS2812B, is selected at build time by
`components/led_driver/Kconfig`. Loading a record cannot switch that backend
with the current driver design.

The sequence record contains no Wi-Fi credentials. Changing the configured LED
count or sequence limits can invalidate a saved sequence at the next boot;
the firmware skips it and waits for a new valid transfer.
