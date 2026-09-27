# LED protocol test matrix

`python3 tools/build_led_variants.py` compiles all seven configurations in
separate ignored `build-<name>/` directories. Compilation checks selection and
linking. It does not prove electrical timing or LED compatibility.

| Variant | Nominal rate | Build | Hardware color/order | Scope timing | Power/current |
| --- | --- | --- | --- | --- | --- |
| WS2812B | 800 kHz | pass | pending | pending | pending |
| SK6812 RGB | 800 kHz | pass | pending | pending | pending |
| WS2811 | 800 kHz | pass | pending | pending | pending |
| WS2811 | 400 kHz | pass | pending | pending | pending |
| UCS1903 | 400 kHz | pass | pending | pending | pending |
| SM16703 | 800 kHz | pass | pending | pending | pending |
| Generic RMT | configured | pass | pending | pending | pending |

For physical testing, set the data GPIO and LED count through `idf.py
menuconfig`, enable the boot diagnostic, and start with a low brightness limit.
Observe red, green, blue, white chase and all-off. Record the actual part,
supply voltage, level shifting, pulse widths, latch time and current. Do not
mark hardware columns passed from a software build alone.
