# ESP32-C3 test configuration

The ESP-IDF Kconfig menus own each setting. `sdkconfig.defaults` records the
non-secret project test profile; the generated, Git-ignored `sdkconfig` holds
machine-local overrides and Wi-Fi credentials. `board_config.h` exposes the
configured pins to C++ and is not a second source of values. No `config.json`
is needed, and no password belongs in a tracked header or JSON file.

| Setting | Current test value | Where to change it |
| --- | --- | --- |
| Target | `esp32c3` | `sdkconfig.defaults`; run `idf.py set-target esp32c3` only before local credentials are entered |
| LED protocol | WS2812B at 800 kHz | `components/led_driver/Kconfig`, selected in `sdkconfig.defaults` or `idf.py menuconfig` |
| LED count | 9 | `components/led_driver/Kconfig`, selected in `sdkconfig.defaults` or `idf.py menuconfig` |
| LED DATA | GPIO4 | `components/config/Kconfig`, selected in `sdkconfig.defaults` or `idf.py menuconfig` |
| I2C, SPI, IMU and microphone pins | Unassigned | Board Kconfig or future component configuration, once wiring is defined |
| Sync role | Station upload server (mDNS) | `components/sync_service/Kconfig`, selected in `sdkconfig.defaults` or `idf.py menuconfig` |
| Target-SSID Wi-Fi diagnostic scan | Enabled in the ignored local test config; normally off | `components/drivers/Kconfig`, toggled with `idf.py menuconfig` |
| Wi-Fi SSID | `Totalplay-81AC` | Ignored local `sdkconfig`, via `idf.py menuconfig` |
| Wi-Fi password | Not shown or tracked | Ignored local `sdkconfig`, via `idf.py menuconfig` |
| TCP upload receiver | ESP32's DHCP address, discovered by Python through mDNS | Station upload server role; no computer IP required |
| Legacy TCP test server | Computer's reachable LAN address, only in Station client mode | Ignored local `sdkconfig`: `LUXFLUX_SYNC_SERVER_HOST_OVERRIDE` |
| TCP port | 3333 | Compatibility constant in `components/sync_service/sync_service.cpp`; advertised through mDNS |

GPIO4 is a candidate general-purpose output on the ESP32-C3; it is not among
the documented strapping pins (2, 8, 9), flash pins (12–17), or USB pins
(18–19). It has alternate functions, so confirm the actual Mini board pinout
and wire the strip DATA line to GPIO4 before claiming visible LED output. Do
not assign GPIO4 to an IMU or audio peripheral later without moving LED DATA
first. See the [Espressif GPIO reference](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32c3/api-reference/peripherals/gpio.html).

The board joins the specified Wi-Fi network, completes scientist enumeration,
and listens for uploads from reachable computers. Python discovers its DHCP
address and port through `_luxflux._tcp.local.`. A laptop can move between
networks without changing a computer IP in the firmware; the board still needs
the correct Wi-Fi credentials. Existing generated `sdkconfig` files keep their
previous role until you explicitly select Station upload server and reflash.
Legacy Station client mode remains available for the Python test server and
uses the configured host override or DHCP gateway.

To edit local values without committing a secret:

```bash
docker compose run --rm esp-idf idf.py menuconfig
docker compose run --rm esp-idf idf.py build
```

Open the LuxFlux LED driver, board configuration, and RGB synchronization
menus. Select Station upload server (mDNS) and enter the Wi-Fi password in the latter. Do
not re-run `set-target` after this: it regenerates `sdkconfig` and may discard
the local credentials. `sdkconfig` and `build/` are excluded by both Git and
the Docker build context; the firmware binary still contains the configured
credential, so share it only with trusted recipients.
