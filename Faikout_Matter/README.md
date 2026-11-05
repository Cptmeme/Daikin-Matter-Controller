# Faikout Matter

> **⚠️ Not tested on hardware yet.** This firmware compiles and links for the boards below, but it has never been booted or paired. Keep a backup of your board's flash before trying it, and expect problems.

Faikout Matter functions just as the original [Faikout firmware](https://codeberg.org/RevK/ESP32-Faikout) by RevK, but adds Matter support over Wi-Fi. It runs on Faikin and Faikout boards, and keeps what Faikout already does: the web interface, MQTT, Home Assistant discovery, BLE sensors and updates over Wi-Fi.

Apart from adding Matter, it differs from the original in three ways: Ethernet boards aren't supported, it's built with ESP-IDF 5.5 rather than 6, and it doesn't download updates from RevK's server.

It's part of [Daikin Matter Controller](../README.md). For a custom ESP32-C6 board on Thread, use the Thread firmware there instead.

This is an independent modified version, not affiliated with or supported by RevK. It won't be merged into the original Faikout repo: it's AI-assisted code, and the Faikout project doesn't accept AI-based bug reports or merge requests. Please report problems [here](https://github.com/Cptmeme/Daikin-Matter-Controller/issues), not to the Faikout project.

## Supported boards

| `build.sh` name | Board | Chip | Flash | PSRAM |
|---|---|---|---|---|
| `pico` | Older Faikin boards | ESP32-PICO-MINI-02 | 8 MB | 2 MB |
| `n4r2` | Current Faikout boards | ESP32-S3-MINI-N4R2 | 4 MB | 2 MB |
| `n8` | Boards using stock firmware `S3-MINI-N8` | ESP32-S3-MINI-N8 | 8 MB | None |

Ethernet boards (FaikoutETH, FaikoutPOE) aren't supported. There's no Thread on any of these boards, because they have no Thread radio.

The `pico` build needs chip revision 3 or newer, the same as stock Faikout. To check your board:

```bash
esptool.py -p /dev/ttyUSB0 flash_id
```

## How it works

1. Faikout starts as usual and connects to your Wi-Fi. Set up a new board through its access point, as with stock Faikout.
2. Matter starts once Wi-Fi is up, and Faikout stays in control of Wi-Fi.
3. Pairing happens over your network, without BLE, so BLE stays free for Faikout's sensors.

Matter commands go through the same functions as Faikout's web interface and MQTT, so they should work with any AC protocol Faikout supports. Matter's device layout, fan slider and setpoint behaviour match the [Thread firmware](../README.md#features).

## Building

Set up ESP-IDF, esp-matter and the SDK patch as in the [main README](../README.md#building--flashing), but install ESP-IDF's tools for these chips with `./install.sh esp32,esp32s3`. You also need `popt`, for Faikout's settings generator:

```bash
brew install popt              # macOS
sudo apt install libpopt-dev   # Debian, Ubuntu
```

Then build for your board:

```bash
git clone https://github.com/cptmeme/Daikin-Matter-Controller.git
cd Daikin-Matter-Controller/Faikout_Matter
./build.sh pico
```

The first build creates a signing key in this folder (`secure_boot_signing_key_*.pem`). **Back it up and keep it private.** A board flashed with this firmware only accepts updates signed with the same key.

## Flashing

The first flash has to be over serial, because stock firmware rejects updates signed with a different key. Back up the board first, so you can go back:

```bash
esptool.py -p /dev/ttyUSB0 read_flash 0 ALL faikout-backup.bin
./build.sh pico -p /dev/ttyUSB0 flash monitor
```

Flashing doesn't erase the settings, so your Wi-Fi details should carry over. Boards without a USB port need a USB-to-serial adapter. Faikout's README mentions a TC2030 lead for programming its boards, so check what yours needs.

## Pairing

1. Open `http://<board address>/matter`. There's also a **Matter pairing** link on the main control page.
2. A board that isn't paired yet stays open for pairing for 15 minutes after it starts. Otherwise, select **Open pairing for 5 minutes**.
3. Add a device in your home app and enter the code shown on the page.

To unpair, remove the device in your home app. Pairing opens again once the last controller is gone. To turn Matter off, clear **Matter** on Faikout's settings page and restart the board.

## Updates

Faikout's own update feature stays, but the update server is left empty, so this firmware never downloads stock Faikout. To use updates over Wi-Fi, host your signed `build-<board>/Faikout.bin` and set `otahost` in the settings. Matter OTA is turned off.

## Known limitations

- **Pairing code:** every board uses the same SDK test code, `3497-011-2332`. Anyone on your network can pair while pairing is open, so only open it when you need it.
- **Certification:** it uses SDK test credentials, so home apps show the device as uncertified. It's for personal use only.
- **Changing Wi-Fi:** change networks in Faikout's settings, not through a Matter controller.
- **Flash space:** the `n4r2` and `n8` builds have only about 12 KB and 16 KB left before they outgrow the update slot. `pico` has about 87 KB.
- **Memory:** `n8` has no PSRAM, so it may run short of RAM, especially with BLE sensors. The Matter page shows free memory.

## Origin and licence

GPL v3, like Faikout. See [LICENSE](LICENSE). Faikout is copyright Adrian Kennard, Andrews & Arnold Ltd. "Faikout" is a registered trademark of its owner, and this project isn't affiliated with them.

This folder contains modified copies of these upstream versions:

| Project | Commit | Date |
|---|---|---|
| [ESP32-Faikout](https://codeberg.org/RevK/ESP32-Faikout) (`main/`) | `fd0db3a7` | 2026-10-27 |
| [ESP32-RevK](https://codeberg.org/RevK/ESP32-RevK) (`components/ESP32-RevK/`) | `8cc4373` | 2026-10-25 |
| [ESP32-BLE-Env](https://codeberg.org/RevK/ESP32-BLE-Env) (`main/bleenv.c`, `main/bleenv.h`) | `6b59fee` | 2026-10-24 |

Changes made on 2026-11-05:

- **Added:** `main/matter_bridge.cpp` and `.h`, `main/CHIPProjectConfig.h`, `build.sh`, the `sdkconfig.defaults.matter*` files and this README.
- **`main/Faikout.c`:** hooks for Matter, a `/matter` web page, and removal of a comment on line 3 aimed at AI tools.
- **`main/settings.def`:** the `matter.enable` setting.
- **`components/ESP32-RevK/revk.c`:** removed RevK's unfinished 2023 Matter workarounds, which turned off the setup access point and mDNS. Also made two event names that only exist in ESP-IDF 6 conditional.
- **Build files:** builds with ESP-IDF 5.5, because esp-matter v1.5 doesn't support ESP-IDF 6. Dropped the `ksz8851snl` Ethernet driver, which needs ESP-IDF 6.
