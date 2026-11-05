#!/bin/sh
# Build the pre-built images. See "Pre-built binaries" in ../README.md.
#
#   ./build_binaries.sh [image ...]        default: all of them
#
# Set up ESP-IDF and esp-matter in this shell first. Each image is built from scratch, and the
# results go in binaries/.

set -e
cd "$(dirname "$0")"

if [ -z "$IDF_PATH" ] || [ -z "$IDF_PYTHON_ENV_PATH" ] || [ -z "$ESP_MATTER_PATH" ]; then
    echo "Set up ESP-IDF and esp-matter in this shell first" >&2
    exit 1
fi
python="$IDF_PYTHON_ENV_PATH/bin/python"

version=$(sed -n 's/^set(PROJECT_VER "\(.*\)")$/\1/p' CMakeLists.txt)
[ -n "$version" ] || { echo "Can't read PROJECT_VER from CMakeLists.txt" >&2; exit 1; }

[ $# -gt 0 ] || set -- thread-c6 thread-c5 wifi-c6 wifi-c5 wifi-s3 wifi-pico
mkdir -p binaries

# Building for other chips rewrites the tracked dependencies.lock, so put it back afterwards
lock=$(mktemp)
cp dependencies.lock "$lock"
trap 'cp "$lock" dependencies.lock; rm -f "$lock"' EXIT

for image in "$@"; do
    case "$image" in
        thread-c6) target=esp32c6; overlay="" ;;   # The same configuration as a plain idf.py build
        thread-c5) target=esp32c5; overlay=sdkconfig.defaults.thread ;;
        wifi-c6)   target=esp32c6; overlay=sdkconfig.defaults.wifi ;;
        wifi-c5)   target=esp32c5; overlay=sdkconfig.defaults.wifi ;;
        wifi-s3)   target=esp32s3; overlay=sdkconfig.defaults.wifi ;;
        wifi-pico) target=esp32;   overlay=sdkconfig.defaults.wifi ;;
        *) echo "Unknown image $image" >&2; exit 1 ;;
    esac

    echo "=== $image"
    rm -rf "build-$image" "sdkconfig.$image"
    "$python" "$IDF_PATH/tools/idf.py" -B "build-$image" -D SDKCONFIG="sdkconfig.$image" -D IDF_TARGET="$target" \
        ${overlay:+-D SDKCONFIG_DEFAULTS="$overlay"} build

    # One file to flash at address 0, and the image for updates over Matter
    name="daikin-matter-$version-$image"
    (cd "build-$image" && "$python" -m esptool --chip "$target" merge_bin -o "../binaries/$name-full.bin" @flash_args)
    cp "build-$image/thermostat-ota.bin" "binaries/$name-ota.bin"
done

(cd binaries && shasum -a 256 *.bin > SHA256SUMS)
ls -l binaries
