#!/bin/sh
# Build or flash this firmware for one board. See README.md.
#
#   ./build.sh <pico|n4r2|n8|c6|c5> [idf.py arguments]  default: build
#   ./build.sh pico -p /dev/ttyUSB0 flash monitor
#
# Set up ESP-IDF and esp-matter in this shell first.

set -e
cd "$(dirname "$0")"

board=$1
case "$board" in
    pico)    target=esp32;   key=secure_boot_signing_key_ecdsa.pem ;;
    n4r2|n8) target=esp32s3; key=secure_boot_signing_key_rsa.pem ;;
    c6)      target=esp32c6; key=secure_boot_signing_key_rsa.pem ;;
    c5)      target=esp32c5; key=secure_boot_signing_key_rsa.pem ;;
    *)       echo "Usage: $0 <pico|n4r2|n8|c6|c5> [idf.py arguments]" >&2; exit 1 ;;
esac
shift
[ $# -gt 0 ] || set -- build

if [ -z "$IDF_PATH" ] || [ -z "$IDF_PYTHON_ENV_PATH" ] || [ -z "$ESP_MATTER_PATH" ]; then
    echo "Set up ESP-IDF and esp-matter in this shell first, see README.md" >&2
    exit 1
fi
python="$IDF_PYTHON_ENV_PATH/bin/python"

# Faikout generates its settings code from the settings.def files. The generator needs popt,
# which Homebrew doesn't put on the default search path.
tool=components/ESP32-RevK/revk_settings
if [ ! -x "$tool" ] || [ "$tool.c" -nt "$tool" ]; then
    popt=$(brew --prefix popt 2>/dev/null || true)
    gcc -O -o "$tool" "$tool.c" -Wall --std=gnu99 ${popt:+-I"$popt/include" -L"$popt/lib"} -lpopt
fi
if [ ! -f main/settings.h ] || [ main/settings.def -nt main/settings.h ] ||
   [ components/ESP32-RevK/settings.def -nt main/settings.h ]; then
    "$tool" main/settings.def components/ESP32-RevK/settings.def
fi

# Updates must be signed with the key the running firmware was built with.
if [ ! -f "$key" ]; then
    case "$key" in
        *rsa*) "$python" -m espsecure generate_signing_key --version 2 --scheme rsa3072 "$key" ;;
        *)     "$python" -m espsecure generate_signing_key --version 1 "$key" ;;
    esac
    echo "Created $key. Back it up: boards flashed with it only accept updates signed with it."
fi

FAIKOUT_BOARD=$board exec "$python" "$IDF_PATH/tools/idf.py" -B "build-$board" -D SDKCONFIG="sdkconfig.$board" -D IDF_TARGET="$target" "$@"
