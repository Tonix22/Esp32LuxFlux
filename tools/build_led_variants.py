#!/usr/bin/env python3
"""Build each compile-time LED backend in its own ESP-IDF build directory."""
import argparse
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
VARIANTS = (
    "ws2812b", "sk6812", "ws2811_800", "ws2811_400",
    "ucs1903", "sm16703", "generic",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", action="store_true", help="use the local ESP-IDF installation")
    parser.add_argument("--variant", choices=VARIANTS, action="append",
                        help="build only the selected variant (repeatable)")
    args = parser.parse_args()
    for name in args.variant or VARIANTS:
        build_dir = f"build-{name}"
        defaults = f"sdkconfig.defaults;tools/variants/{name}.defaults"
        command = ["idf.py", "-B", build_dir,
                   "-D", f"SDKCONFIG={build_dir}/sdkconfig",
                   "-D", f"SDKCONFIG_DEFAULTS={defaults}", "build"]
        if not args.native:
            command = ["docker", "compose", "run", "--rm", "esp-idf", *command]
        print(f"Building {name}: {' '.join(command)}", flush=True)
        subprocess.run(command, cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
