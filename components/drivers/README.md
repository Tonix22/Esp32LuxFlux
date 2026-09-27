# Wi-Fi driver

This component initializes ESP-IDF Wi-Fi explicitly and supports Station,
SoftAP and combined mode. It handles Station connection events and reconnects
after a disconnect. No radio starts from a global constructor. Credentials
come from local Kconfig values in the ignored sdkconfig. The Station gateway
is read from DHCP, and `stop()` tears down interfaces and event handlers.
Station configuration explicitly accepts WPA2-PSK or stronger and disables
Wi-Fi power saving, following the read-only ESP32WOL `main/main.c` pattern.
The optional `LUXFLUX_WIFI_DIAGNOSTIC_SCAN` Kconfig setting scans the target
SSID before connecting and logs channel, RSSI, security mode and disconnect
reasons without logging the password. It is enabled only in the ignored local
`sdkconfig` during bring-up; the tracked default remains off.
