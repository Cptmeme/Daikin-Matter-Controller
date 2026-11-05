# Faikout Matter

> **⚠️ Only partly tested.** On one board, a Faikin with an ESP32-PICO-MINI-02, it boots, joins Wi-Fi, shows its web page and pairs with a home app. It hasn't been connected to an AC yet, so controlling one is untested. The other boards have only been compiled. Keep a backup of your board's flash before trying it, and expect problems.

Faikout Matter functions just as the original [Faikout firmware](https://codeberg.org/RevK/ESP32-Faikout) by RevK, but adds Matter support over Wi-Fi. It runs on Faikin and Faikout boards and on the custom PCB in this repo, and keeps what Faikout already does: the web interface, MQTT, Home Assistant discovery, BLE sensors and updates over Wi-Fi.

Apart from adding Matter, it differs from the original in two ways: it's built with ESP-IDF 5.5 rather than 6, and it doesn't download updates from RevK's server.

It's part of [Daikin Matter Controller](../README.md). To use the custom PCB on Thread, use the original firmware there instead.

This is an independent modified version, not affiliated with or supported by RevK. It won't be merged into the original Faikout repo: it's AI-assisted code, and the Faikout project doesn't accept AI-based bug reports or merge requests. Please report problems [here](https://github.com/Cptmeme/Daikin-Matter-Controller/issues), not to the Faikout project.

## Supported boards

| `build.sh` name | Board | Chip | Flash | PSRAM |
|---|---|---|---|---|
| `pico` | Older Faikin boards | ESP32-PICO-MINI-02 | 8 MB | 2 MB |
| `n4r2` | Current Faikout boards | ESP32-S3-MINI-N4R2 | 4 MB | 2 MB |
| `n8` | Boards using stock firmware `S3-MINI-N8` | ESP32-S3-MINI-N8 | 8 MB | None |
| `eth` | FaikoutETH | ESP32-S3-MINI-1-N4R2 with Ethernet | 4 MB | 2 MB |
| `poe` | FaikoutPOE | ESP32-S3-MINI-1-N4R2 with Ethernet | 4 MB | 2 MB |
| `c6` | The [custom PCB](../Custom%20PCB%20esp32c6) in this repo | WT0132C6-S5 (ESP32-C6) | 4 MB | None |
| `c5` | The custom PCB with an ESPC5-12 module instead | ESPC5-12 (ESP32-C5) | 4 MB | None |

FaikoutIR isn't supported, because Faikout has no IR code yet. This firmware doesn't use Thread on any board, including the C6 and C5.

On `eth` and `poe`, Matter starts on whichever network comes up first, Ethernet or Wi-Fi. They use ESP-IDF's built-in Ethernet driver, and the reset pins come from each board's PCB file: GPIO 9 on `eth` and GPIO 16 on `poe`. These two builds have the least testing behind them, because Matter's SDK here is built for Wi-Fi.

The `pico` build needs chip revision 3 or newer, the same as stock Faikout. To check your board:

```bash
esptool.py -p /dev/ttyUSB0 flash_id
```

### The custom PCB

| | `c6` | `c5` |
|---|---|---|
| **S21 TX** | GPIO 21 | GPIO 10 |
| **S21 RX** | GPIO 20 | GPIO 9 |
| **Button** | GPIO 23 | GPIO 14 |

These builds differ from the other boards in a few ways:

- **Button:** it works as on Faikout. Press once to restart, twice to start an update, and 3 times to erase the settings, including Wi-Fi and Matter pairings. It doesn't open pairing as it does on the original firmware, so use the Matter page.
- **Console:** logs go to the `RX0` and `TX0` header, because the board has no USB port. It has no status LED.
- **Size:** some code is left out so they fit in 4 MB. See [below](#whats-left-out-of-the-c6-c5-eth-and-poe-images).
- **Switching firmware:** moving a board between this and the original firmware takes a serial flash, because the partition layouts differ. Pair it again afterwards.

On the `c5` board, GPIO 14 is also the USB D+ pin, so test the button first. The ESPC5-12's ground pad also stays unsoldered, because the PCB's footprint doesn't have one, and the S21 vias overlap the edge of that pad. Check `RX1` and `TX1` to GND with a meter after soldering.

### What's left out of the `c6`, `c5`, `eth` and `poe` images

These images have parts stripped out to save flash space. This shouldn't break anything, because Faikout doesn't use these parts, but that comes from reading the code and hasn't been tested on hardware yet. The `pico`, `n4r2` and `n8` images aren't affected.

| Left out | Why it shouldn't matter |
|---|---|
| BLE connections, BLE 5 features and BLE services | Faikout only scans for sensors and advertises, and that stays |
| Enterprise Wi-Fi, OWE, SAE-PK, and WPA3 on the setup access point | Faikout has no settings for them. WPA2 and WPA3 on your own network stay |
| TLS server, DTLS, pre-shared keys and static ECDH key exchange | Faikout only connects out, for MQTT and updates. TLS 1.2 with ECDHE or RSA stays, and so do client certificates and the CA bundle |
| The P-192, P-224, P-521, secp256k1 and Brainpool curves | P-256, P-384 and Curve25519 stay. The one case that would fail is a server whose certificate uses a removed curve |
| Assert messages, error names and TLS error text | Logs get less detailed, but nothing changes in how the firmware behaves |
| The `esp_secure_cert` and `fctry` partitions | This firmware doesn't use them |

The full list is in [`sdkconfig.defaults.matter-small`](sdkconfig.defaults.matter-small) and [`partitions_4m_matter.csv`](partitions_4m_matter.csv).

## How it works

1. Faikout starts as usual and connects to your Wi-Fi. Set up a new board through its access point, as with stock Faikout.
2. Matter starts once Wi-Fi is up, and Faikout stays in control of Wi-Fi.
3. Pairing happens over your network, without BLE, so BLE stays free for Faikout's sensors.

Matter commands go through the same functions as Faikout's web interface and MQTT, so they should work with any AC protocol Faikout supports. Matter's device layout, fan slider and setpoint behaviour match the [original firmware](../README.md#features).

## Building

Set up ESP-IDF, esp-matter and the SDK patch as in the [main README](../README.md#building--flashing), but install ESP-IDF's tools for these chips with `./install.sh esp32,esp32s3,esp32c5,esp32c6`. You also need `popt`, for Faikout's settings generator:

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

Faikout's own update feature stays, but the update server is left empty, so this firmware never downloads stock Faikout. Automatic updates are off too. To use updates over the network, host your signed `build-<board>/Faikout.bin` and set `otahost` in the settings, then turn on **Auto upgrade** if you want them to be automatic. Matter OTA is turned off.

## Known limitations

- **Pairing code:** every board uses the same SDK test code, `3497-011-2332`. Anyone on your network can pair while pairing is open, so only open it when you need it.
- **Certification:** it uses SDK test credentials, so home apps show the device as uncertified. It's for personal use only.
- **Changing Wi-Fi:** change networks in Faikout's settings, not through a Matter controller.
- **Flash space:** `n4r2` and `n8` are close to outgrowing their update slots, with about 12 KB and 16 KB left, and so is `c5` with about 13 KB. `c6` has about 51 KB, `pico` about 87 KB, and `eth` and `poe` about 308 KB.
- **Memory:** `n8`, `c6` and `c5` have no PSRAM, so they may run short of RAM, especially with BLE sensors. Even with PSRAM, `pico` had only 46 KB of internal memory free once paired. The Matter page shows free memory.

## Origin and licence

GPL v3, like Faikout. See [LICENSE](LICENSE). Faikout is copyright Adrian Kennard, Andrews & Arnold Ltd. "Faikout" is a registered trademark of its owner, and this project isn't affiliated with them.

This folder contains modified copies of these upstream versions:

| Project | Commit | Date |
|---|---|---|
| [ESP32-Faikout](https://codeberg.org/RevK/ESP32-Faikout) (`main/`) | `fd0db3a7` | 2026-10-27 |
| [ESP32-RevK](https://codeberg.org/RevK/ESP32-RevK) (`components/ESP32-RevK/`) | `8cc4373` | 2026-10-25 |
| [ESP32-BLE-Env](https://codeberg.org/RevK/ESP32-BLE-Env) (`main/bleenv.c`, `main/bleenv.h`) | `6b59fee` | 2026-10-24 |

Changes made on 2026-11-05:

- **Added:** `main/matter_bridge.cpp` and `.h`, `main/CHIPProjectConfig.h`, `build.sh`, `partitions_4m_matter.csv`, the `sdkconfig.defaults.matter*` files and this README.
- **`main/Faikout.c`:** hooks for Matter, a `/matter` web page, and removal of a comment on line 3 aimed at AI tools.
- **`main/settings.def`:** the `matter.enable` setting, and the S21 pins of the custom PCB as the C5 and C6 defaults.
- **`components/ESP32-RevK/settings.def`:** the custom PCB's button as the C5 and C6 default, and automatic updates off by default.
- **`components/ESP32-RevK/revk.c`:** removed RevK's unfinished 2023 Matter workarounds, which turned off the setup access point and mDNS. Also made two event names that only exist in ESP-IDF 6 conditional.
- **Build files:** builds with ESP-IDF 5.5, because esp-matter v1.5 doesn't support ESP-IDF 6. Uses ESP-IDF 5's built-in Ethernet driver instead of the `ksz8851snl` package, which needs ESP-IDF 6.
