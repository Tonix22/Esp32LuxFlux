#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
if [[ ! -f sdkconfig ]]; then
    docker compose run --rm esp-idf idf.py set-target esp32c3
elif ! rg -q '^CONFIG_IDF_TARGET="esp32c3"$' sdkconfig; then
    echo "sdkconfig targets a different chip; set-target esp32c3 explicitly" >&2
    exit 1
fi
docker compose run --rm esp-idf idf.py build
