# Daikin Matter Controller

[![License: GPL v3](https://img.shields.io/badge/License-GPL_v3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.en.html)
[![Stars](https://img.shields.io/github/stars/cptmeme/Daikin-Matter-Controller?style=social)](../../stargazers)

> **⚠️ Disclaimer.** Opening your air conditioner exposes you to mains voltage and can damage the unit. Always disconnect mains power at the breaker before opening the indoor unit. Incorrect wiring on the S21 bus can damage the AC's controller board or the ESP32. You assume all responsibility for any damage, data loss, or device failure. This project is not affiliated with Daikin Industries, the Connectivity Standards Alliance, Espressif Systems, or RevK / the Faikin/Faikout project.

Open source Matter firmware for Daikin split air conditioners with an S21 port. Adds your AC to Apple Home, Google Home, Alexa, and Home Assistant — no Daikin app, no cloud, no BRP module. **Most modern Daikin split units have an S21 connector inside the indoor unit waiting to be used.**

> Both firmwares below appear as a **Room Air Conditioner** — Matter device type `0x0072`, with **OnOff, Thermostat, Fan Control, Temperature Measurement, and Electrical Power/Energy Measurement** clusters bundled into a single tile. Power, mode (Heat / Cool / Auto / Off, plus Dry and Fan), setpoint, current temperature, fan speed, and power and energy use all surface as native Matter attributes.
>
> **Powerful mode** is exposed on a second endpoint as an OnOff plug-in unit. In Apple Home and Google Home it appears as an "Outlet" tile next to the AC tile — rename it to "Powerful" in your home app.

> ⚠️ Uses ESP-Matter SDK test credentials (vendor `0xFFF1`, not VID/PID-certified). Functional for personal use; not suitable for resale as a certified Matter product.

---

## Firmware options

This repo has two firmwares. Pick one based on your board:

| | Original firmware | Faikout Matter |
|---|---|---|
| **Choose it if** | You just want Matter on your AC, over Thread or Wi-Fi | You want to keep Faikout's features as well |
| **Network** | Thread or Wi-Fi, depending on the image | Wi-Fi, or Ethernet on boards that have it |
| **Board** | The custom PCB in this repo, Faikout boards and older Faikin boards | The same, plus FaikoutETH and FaikoutPOE |
| **What it is** | Its own firmware, using Faikout's S21 protocol code | The original Faikout firmware, with Matter support added |
| **Also has** | Matter only | Everything in Faikout: web interface, MQTT, Home Assistant discovery and BLE sensors |
| **Pairing** | Over BLE | Over your network |
| **Updates** | Matter OTA | Faikout's own updates |
| **Pre-built images** | [Yes](#pre-built-binaries) | No, build it yourself |
| **Folder** | [`Thermostat_Daikin`](Thermostat_Daikin) | [`Faikout_Matter`](Faikout_Matter) |
| **Status** | Thread on the WT0132C6 runs on real ACs. **The other images are unverified** | Boots and pairs on a PICO board, but **not yet tested with an AC** |

Faikout Matter has [its own README](Faikout_Matter/README.md). It's AI-assisted code and won't be merged into the original Faikout repo, because the Faikout project doesn't accept AI-based bug reports or merge requests.

Unless a section says otherwise, the rest of this page covers the original firmware.

---

## Contents

- [Firmware options](#firmware-options)
- [Faikout Matter](Faikout_Matter/README.md) (separate README)
- **Original firmware**
  - [Quick Start](#quick-start)
  - [Pre-built binaries](#pre-built-binaries)
  - [Features](#features)
  - [Compatible Hubs](#compatible-hubs)
  - [Hardware](#hardware)
  - [Pin configuration](#pin-configuration)
  - [Building & Flashing](#building--flashing)
  - [Commissioning](#commissioning)
  - [Updating the Firmware](#updating-the-firmware)
  - [Factory Reset](#factory-reset)
  - [Customizing the device name](#customizing-the-device-name)
- **Both firmwares**
  - [S21 connector pinout](#s21-connector-pinout)
  - [Known Issues / Roadmap](#known-issues--roadmap)
  - [License](#license)
  - [Matter and Thread Certification](#matter-and-thread-certification)
  - [Contributing](#contributing)
  - [Why?](#why)

---

## Quick Start

**If your board is listed under [Pre-built binaries](#pre-built-binaries), you can flash one of those. Otherwise, build from source:**

1. Install ESP-IDF and ESP-Matter — follow Espressif's [Getting the Repositories](https://docs.espressif.com/projects/esp-matter/en/latest/esp32/developing.html) guide.
2. Connect your interface board to the Daikin's S21 port — see [Hardware](#hardware).
3. Edit the GPIO pin defines in [`main/app_driver.cpp`](Thermostat_Daikin/main/app_driver.cpp) to match your wiring (see [Pin configuration](#pin-configuration)).
4. Build, flash, and monitor — see [Building & Flashing](#building--flashing).
5. [Commission](#commissioning) with your smart home app.

---

## Pre-built binaries

> ⚠️ **These binaries are not verified.** Only `thread-c6` has run on a real AC. The others have only been compiled, because I don't have most of these boards to test them on. Expect problems, and back up your board's flash first.

Download them from [Releases](../../releases).

| Image | Board | Network | S21 TX / RX | Button | Tested |
|---|---|---|---|---|---|
| `thread-c6` | Custom PCB with a WT0132C6-S5 | Thread | GPIO 21 / 20 | GPIO 23 | ✅ On real ACs |
| `wifi-c6` | Custom PCB with a WT0132C6-S5 | Wi-Fi | GPIO 21 / 20 | GPIO 23 | ❌ |
| `thread-c5` | Custom PCB with an ESPC5-12 | Thread | GPIO 10 / 9 | GPIO 14 | ❌ |
| `wifi-c5` | Custom PCB with an ESPC5-12 | Wi-Fi | GPIO 10 / 9 | GPIO 14 | ❌ |
| `wifi-s3` | Faikout, and the Faikin+, Faikin2 and final Faikin S3 boards | Wi-Fi | GPIO 48 / 34 | GPIO 21 (Reset header) | ❌ |
| `wifi-pico` | Faikin with an ESP32-PICO-MINI-02 | Wi-Fi | GPIO 26 / 27 | GPIO 0 (programming header) | ❌ |

The pins come from each board's PCB files. These boards won't work with any image:

- **Faikin3 with an ESP32-S3:** S21 is on GPIO 47 and 21.
- **FaikoutETH and FaikoutPOE:** this firmware has no Ethernet, and the POE board also needs a load pin driven. Use [Faikout Matter](Faikout_Matter/README.md).
- **FaikoutIR:** it has no S21 port.

Each image comes as two files, and `SHA256SUMS` lists their checksums:

- **`-full.bin`** is for flashing over serial. It replaces everything up to the end of the app, **including your pairings and Wi-Fi details**:

  ```bash
  esptool.py --chip esp32c6 -p <PORT> write_flash 0x0 daikin-matter-1.6-thread-c6-full.bin
  ```

  Use `esp32c6`, `esp32c5`, `esp32s3` or `esp32` for `--chip`, to match the board.
- **`-ota.bin`** is for [updates over Matter](#over-the-air-matter-ota-over-thread). Only send a board the image it's already running. A Wi-Fi image sent to a board on Thread would take it off its network.

The Wi-Fi images pair like any Matter device: your home app connects over BLE and passes on your Wi-Fi details.

---

## Features

Faikout Matter offers the same Matter features, apart from the reset button and Matter OTA. Powerful and power and energy are opt-in there, along with switches for its other modes: see [what it exposes](Faikout_Matter/README.md#what-it-exposes-over-matter). It hasn't been tested with an AC yet.

- ✅ Matter over Thread — uses the chip's 802.15.4 radio, no WiFi required. Or Matter over Wi-Fi, depending on the image
- ✅ Compatible with Apple Home, Google Home, Alexa, Home Assistant, etc.
- ✅ Multi-fabric — commission to multiple hubs simultaneously (up to 5 fabrics, and Apple Home alone uses 2)
- ✅ Native Room Air Conditioner tile (single accessory in your home app)
- ✅ Power, Mode (Heat/Cool/Auto/Off, plus Dry and Fan for controllers that support them), Setpoint, Current Temperature
- ✅ Fan speed (Quiet, levels 1 to 5, and Auto on a percent slider)
- ✅ Powerful mode as a separate switchable endpoint
- ✅ Live power draw (W) + lifetime energy (kWh) on S21 units that report it — surfaced via Matter Electrical Power/Energy Measurement clusters (Home Assistant displays them and can feed its Energy dashboard)
- ✅ Two-way state sync — adjusting on the Daikin remote updates your home app within about 5 seconds (up to about 20 seconds if it comes right after a command from your home app)
- ✅ Factory reset via long press on the onboard button
- ✅ OTA-ready — dual OTA partitions with bootloader app-rollback for safe over-the-air updates (via a Matter OTA provider)

> **Fan speed visibility note:** Apple Home on iOS 27.2 shows the fan slider in the AC's device settings, not on the tile. It may have appeared as early as iOS 27.0, but that's unconfirmed, and older versions don't show it. Google Home on iOS doesn't show it either. Home Assistant does. A missing slider is a controller-side UI limitation, not a firmware bug — the FanControl cluster is correctly registered and writable from any Matter controller that chooses to surface it.
>
> **Fan slider scale:** 0 is Quiet, 1–99 steps through levels 1 to 5 (a new level about every 20%), and 100 is Auto. Apple Home has no separate options for Quiet and Auto, so they sit at the two ends of the slider. A controller that shows fan modes may label Quiet as Off, because Matter treats 0% as Off, but the fan doesn't stop.
>
> **Setpoints:** Matter has separate heat and cool setpoints, but the AC only has one target. While the AC is on, only the setpoint for the current mode applies, and switching between heat and cool applies that mode's stored setpoint. In Auto, or while the AC is off, the latest setpoint you set wins.

---

## Compatible Hubs

> **Requires a Thread Border Router.** A Thread Border Router is required for any Matter-over-Thread device.

These results are for the original firmware on Thread. The Wi-Fi images and Faikout Matter don't need a Thread Border Router. They haven't been tested with these controllers, except that Faikout Matter has paired with a home app.

| Ecosystem | Tested |
|-----------|--------|
| Apple Home | ✅ (fan speed in device settings on iOS 27.2)*|
| Google Home | ✅ (no fan speed control on IOS)|
| Alexa | Untested |
| Home Assistant | ✅ |
| SmartThings | Untested |

>*On older iOS versions, fan speed can still be controlled through the Eve app or automations in the Home app
---
### Hardware

**Recommended path: use a known-good interface board**

You need *something* between the ESP32-C6 and the Daikin's S21 port — the S21 bus is 5V logic and the ESP32 is 3.3V. A standard bidirectional level shifter (e.g. BSS138-based, like Adafruit / SparkFun "logic level converter" boards) can work for the data lines if wired correctly, but see the polarity warning below: the included PCB also uses BSS138 FETs, wired as inverters rather than as a bidirectional shifter. The reason for recommending a known-good board over breadboarding is more about getting the power supply, connectors, and protection right than about the level shifting being especially tricky.

> ⚠️ **Important — the included PCB inverts the S21 signals in hardware, and the firmware is written to match.** The S21 UART in [`main/s21_driver.cpp`](Thermostat_Daikin/main/s21_driver.cpp) runs with both lines inverted (see `uart_set_line_inverse` in `s21_line_init`). If you wire up a generic *non-inverting* level shifter, the AC will receive the wrong polarity and communication will silently fail. You then have two choices:
>
> - **Add a hardware inverter** in series with each S21 line (single transistor or a hex inverter IC like 74HC04), or
> - **Edit the firmware** to remove the inversion — delete the `uart_set_line_inverse` call in [`main/s21_driver.cpp`](Thermostat_Daikin/main/s21_driver.cpp). The experimental backends have their own settings: `X50A_INVERT_LINES`, `CNWIRED_RX_INVERT` and `CNWIRED_TX_INVERT`.
>
> If you're using the included PCB, a Faikout, or any other interface that expects inverted-polarity drive, leave the firmware alone — it's already correct.
>
> The included PCB is derived from RevK's [Faikout](https://codeberg.org/RevK/ESP32-Faikout) hardware, which is where the inverted-signal convention comes from. For the deeper "why" — schematic excerpts, component choices, and the historical context for why S21 interfaces are typically built this way — see RevK's repos on [Codeberg](https://codeberg.org/RevK).

Two interface options:

1. **The reference PCB included in this repo, known to work end-to-end** — see the [`Custom PCB esp32c6/`](Custom%20PCB%20esp32c6/) folder. KiCad project + BOM. Built around a WT0132C6-S5, a BSS138 for level shifting, and a JST EH 5-way for the S21 connection. **Thread + Wi-Fi capable** thanks to the C6's 802.15.4 radio, and it can run either firmware. *Note: This board does not feature an onboard USB port. Flashing is done via exposed `RX0` and `TX0` headers using an external USB-to-Serial adapter, and the board is powered via the `V_in` pin, which accepts **4.5V to 17V** (the limits of its TPS562246 regulator, whose absolute maximum is 19V). The 12-14V from S21 pin 4 is well within that.*
2. **A RevK [ESP32-Faikout](https://codeberg.org/RevK/ESP32-Faikout) or older Faikin board.** Use [Faikout Matter](Faikout_Matter/README.md) to keep Faikout's firmware and add Matter, or the `wifi-s3` or `wifi-pico` [pre-built binary](#pre-built-binaries) of this firmware. Faikout boards are sold by RevK and on Amazon UK — check the Faikout repo for current availability.

> **⚠️ Faikout boards = Matter over Wi-Fi only.** Current Faikout PCBs use ESP32 / ESP32-S3 chips, neither of which has an 802.15.4 radio, so they can't use Thread and you lose its mesh / low-power benefits. A future Faikout revision based on the ESP32-C6 (or similar) would change this — track the Faikout repo if that matters to you.
>
> This firmware's Wi-Fi builds for these boards haven't been run on one yet. See [Pre-built binaries](#pre-built-binaries) for which boards their pins match.

If you want to roll your own interface, use the included PCB as a reference rather than starting from scratch.

### S21 connector pinout

This section and the next apply to both firmwares.

Pinout, colors, and connector types are documented authoritatively in [Faikin's wiring wiki](https://codeberg.org/RevK/ESP32-Faikout/wiki/Wiring). Reproduced for convenience:

| Pin | Daikin color | Generic color | Function |
|-----|--------------|---------------|----------|
| 1 | Brown | Orange | 5V (not always connected; insufficient current for the board) |
| 2 | Red | Blue | TX from aircon (5V logic) |
| 3 | Orange | Yellow | RX to aircon (5V logic; most models accept 3.3V) |
| 4 | Yellow | Red | 12-14V (the actual power source — both Faikout and the included PCB run from this pin) |
| 5 | Blue | Black | GND |

> **Pin direction is from the aircon's perspective.** Pin 2 is the aircon's TX (data flowing *out* of the AC, *into* the ESP32). Pin 3 is the aircon's RX (data flowing *into* the AC, *from* the ESP32). The included PCB / Faikout handles the crossover correctly.

> **Power comes from pin 4, not pin 1.** Pin 1's 5V rail does not supply enough current to run the ESP32-C6 + interface logic. The included PCB uses a [TI TPS562246](https://www.ti.com/product/TPS562246) synchronous step-down (buck) converter to take the 12-14V from pin 4 down to 3.3V for the ESP32, and it accepts **4.5V to 17V**. Faikout boards use a different regulator, and RevK's [wiring wiki](https://codeberg.org/RevK/ESP32-Faikout/wiki/Wiring) rates them for 4V to 36V on pin 4, so don't assume the included PCB can take what a Faikout can. **Do not try to power the board from pin 1.**

**Two physical connector variants exist on the Daikin side:**
- **Type A** — JST EH (2.5 mm pitch), older models
- **Type B** — JST PAP (2.0 mm pitch), newer models

The boards above use JST EH male (5-way). For Type B units you need either a Type-A-to-Type-B cable or a different connector on the board. Faikin's wiring wiki links to per-type cable drawings.

### Where to find the S21 port on your AC

- [Faikin's confirmed-working models list](https://codeberg.org/RevK/ESP32-Faikout/wiki/List-of-confirmed-working-air-con-units) — start here. If your model is on this list it has S21.
- [Faikin's FTXM wiring teardown](https://codeberg.org/RevK/ESP32-Faikout/wiki/FTXMxxW2VMA-Wiring) — step-by-step photos for the FTXM series, the most common Daikin split.
- For other models the S21 port is generally on the indoor unit's main controller PCB, accessible after removing the front cover and electrical-bay cover.

### ESP32-C6 GPIO assignments (firmware side)

| ESP32-C6 GPIO | Function |
|---------------|----------|
| GPIO 20 | S21 RX (input) — receives data from the AC |
| GPIO 21 | S21 TX (output) — transmits to the AC |
| GPIO 23 | Factory-reset / commissioning button → GND (active low, internal pull-up) |

> **GPIO TX/RX naming is from the ESP32's perspective.** GPIO21 is an output, GPIO20 is an input. This is the opposite of the S21 connector's pin labels (which are from the AC's perspective). The included PCB and Faikout both wire the crossover correctly — you don't need to think about it if you're using either board.

### Factory-reset button

Hold the GPIO23 button for at least 5 seconds and then release it to trigger a Matter factory reset (clears all fabrics and re-enters BLE commissioning mode). A shorter press opens a new 5-minute commissioning window if no fabric is paired, and does nothing once the device is commissioned.

---

## Pin configuration

Unless your hardware matches a board under [Pre-built binaries](#pre-built-binaries), you'll need to change the GPIO pin assignments to match your wiring before building. They're defined per chip as preprocessor macros in [`main/app_driver.cpp`](Thermostat_Daikin/main/app_driver.cpp). For the ESP32-C6:

```c
#define S21_TX_PIN 21        // ESP32 → S21 RX  (output)
#define S21_RX_PIN 20        // ESP32 ← S21 TX  (input)
#define BUTTON_GPIO_PIN 23   // factory-reset / commissioning button → GND
```

Pick GPIOs that are safe to use as general-purpose IO on your specific ESP32 variant (avoid strapping pins, JTAG pins, USB pins, etc.). For ESP32-C6 the [Espressif datasheet pinout table](https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf) is the authoritative reference.

If you're using a board where the buttons or LEDs are on different pins than the included PCB, you may also need to adjust them in `app_driver.cpp` and `app_main.cpp`.

---

## Building & Flashing

> **Do you need to build it?** This firmware has its GPIO pins compiled in (see above). If your board isn't listed under [Pre-built binaries](#pre-built-binaries), those images would have the wrong pins for your wiring, and could silently fail to talk to the AC. In that case, build from source.

### Hardware setup for flashing

Because the included PCB does not have an onboard USB port, you will need a USB-to-Serial (UART) adapter (like an FTDI or CH340 board) to flash the ESP32-C6.

> ⚠️ **CRITICAL: 3.3V Logic vs. 5V Power**
> The ESP32-C6 requires **3.3V logic** on its `RX0` and `TX0` pins. Applying 5V data signals will permanently damage the chip. However, the board itself must be powered with at least 4.5V via the `V_in` pin so the onboard step-down converter can function.
> 
> **You must use a UART adapter set to 3.3V logic, but use a 5V pin for power.** Many standard FTDI adapters have a jumper or switch to set the TX/RX logic levels to 3.3V while still exposing a dedicated 5V VCC pin. Do not use a 3.3V power source for `V_in`.

**Wiring your UART adapter:**
* **TX** on adapter (3.3V logic) ➔ **RX0** on PCB
* **RX** on adapter (3.3V logic) ➔ **TX0** on PCB
* **GND** on adapter ➔ **GND** on PCB
* **5V** on adapter (Power) ➔ **V_in** on PCB 

Make sure to put your ESP32-C6 into **Download Mode** before flashing (typically by holding the BOOT button/pin low while pulling the EN Reset pin low, then releasing EN, then releasing BOOT).

### 1. Install ESP-IDF and ESP-Matter

Follow Espressif's official setup: **[Getting the ESP-Matter Repositories](https://docs.espressif.com/projects/esp-matter/en/latest/esp32/developing.html)**. The short version on macOS / Linux:

```bash
mkdir -p ~/esp && cd ~/esp
git clone -b v5.5.4 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32c6 && cd ..
git clone -b release/v1.5 --depth 1 https://github.com/espressif/esp-matter.git
cd esp-matter && ./install.sh && cd ..
```

**Versions.** This firmware is built against **ESP-Matter v1.5**. Recent releases have been built and tested with **ESP-IDF v5.5.4, which the commands above install and which requires the SDK patch below** (its `@`-response-file compiler flags otherwise break connectedhomeip's GN argument generation). ESP-IDF v5.4.1 is ESP-Matter v1.5's officially-supported pairing and doesn't need the patch, but the current code hasn't been tested on it.

### 1a. Apply the ESP-Matter SDK patch

A small fix is needed **in the ESP-Matter SDK itself** — it lives under `~/esp/esp-matter/…`, *outside this repo*, so apply it after cloning esp-matter (and re-apply if you ever update or re-install esp-matter):

**Patch — GN build fix (required on ESP-IDF 5.5.x; not needed on 5.4.1).**
Without it, the build aborts with `ERROR at build arg file … Invalid token`. IDF 5.5 puts flags such as `-march` into a GCC `@response-file`, and connectedhomeip's `create_args_gn.py` wraps the whole `@"…"` token in quotes, which GN can't parse. Edit
`connectedhomeip/connectedhomeip/config/esp32/components/chip/create_args_gn.py` — immediately after the line `compile_flags = compile_command.split()[1:-4]`, expand any `@`-file back into real flags:

```python
expanded = []
for f in compile_flags:
    if f.startswith('@'):
        try:
            with open(f[1:].strip('"')) as rsp:
                expanded.extend(rsp.read().split())
            continue
        except OSError:
            pass
    expanded.append(f)
compile_flags = expanded
```

Then force a regen of the CHIP build: `rm -rf build/esp-idf/chip` and rebuild.

### 2. Configure your environment in every new shell

```bash
source ~/esp/esp-idf/export.sh
source ~/esp/esp-matter/export.sh
```

### 3. Build and flash

```bash
git clone https://github.com/cptmeme/Daikin-Matter-Controller.git
cd Daikin-Matter-Controller/Thermostat_Daikin

# Edit main/app_driver.cpp first if your pins differ from the included PCB.

idf.py set-target esp32c6
idf.py build
idf.py -p <PORT> flash monitor
```

Replace `<PORT>` with your serial port (`/dev/cu.usbserial-XXXX` on macOS, `/dev/ttyUSB0` on Linux, `COM3` on Windows).

To build all the pre-built images instead, install the tools for every chip with `./install.sh esp32,esp32s3,esp32c5,esp32c6` and run `./build_binaries.sh`. The results go in `binaries/`.

> ⚠️ **Disconnect mains power at the breaker before opening the indoor unit.** The S21 port itself is logic-level and isolated from mains, but everything around it inside the cabinet is not. If you're not comfortable working inside an AC indoor unit, have a qualified technician install the cable for you and just plug into the S21 connector once it's accessible.

### 4. Verify the firmware is running

In the `idf.py monitor` output, after the boot banner you should see:

```
I (xxxx) app_main: Room Air Conditioner created with endpoint_id 1
I (xxxx) app_main: Powerful plug-in unit created with endpoint_id 2
I (xxxx) app_main: Commissioning window opened
```

This confirms Matter is initialized and the device is advertising for commissioning over BLE.

## Commissioning

Faikout Matter pairs over your network instead, with its own code and QR code on its Matter settings tab: see [its pairing steps](Faikout_Matter/README.md#pairing).

- Commission devices ONE AT A TIME.
- Power off or move away other un-commissioned devices while setting up each one.
- All devices share the same pairing code, so the controller may get confused if multiple are advertising simultaneously.
- Once commissioned, each device gets a unique identity on your network.

### Apple Home

1. Open the **Home** app.
2. Tap **+** → **Add Accessory**.
3. Tap **More Options** to enter the setup code manually.
4. Press **Add Anyway** when prompted (this is expected — see [Matter and Thread Certification](#matter-and-thread-certification)).
5. Name the device and place it in a room. You'll see two tiles: the AC and the "Outlet" (Powerful). Rename the Outlet to "Powerful" or similar.

```
Setup code: 3497-011-2332
```

> ⚠️ This is the ESP-Matter SDK test setup code, used by all devices running this firmware. Once a device is commissioned to your Matter fabric, the setup code is no longer used for authentication — your Matter ecosystem manages credentials going forward. Multiple uncommissioned devices broadcasting the same setup code is why the commissioning instructions specify one device at a time.

> **Apple Home creates two fabrics during pairing.** This is normal. Apple Inc. (vendor `0x1349`) is the local Home Hub admin; Apple Keychain (vendor `0x1384`) is Apple's iCloud-based admin that lets all your Apple devices control the unit. Both fabrics are Apple's — you have not been auto-shared with a third party.

<details>
<summary>Google Home / Alexa / Home Assistant commissioning</summary>

Google Home, Alexa, and Home Assistant all support Matter commissioning via their respective apps and integrations. Use setup code `3497-011-2332` when prompted.

**Home Assistant renders the fan slider** for this device. Apple Home shows it in the AC's device settings on iOS 27.2, not on the tile.

</details>

---

## Updating the Firmware

Faikout Matter doesn't use Matter OTA: see [its update notes](Faikout_Matter/README.md#updates).

### Versions

Bump all three for every release, or the device ignores the update. The build fails if the two numbers differ.

* `PROJECT_VER` (e.g. `"1.6"`) and `PROJECT_VER_NUMBER` (e.g. `7`) in `Thermostat_Daikin/CMakeLists.txt`
* `CONFIG_DEVICE_SOFTWARE_VERSION_NUMBER` in `Thermostat_Daikin/sdkconfig.defaults` (same number)

`sdkconfig` is generated from the defaults files and isn't tracked in git. An existing `sdkconfig` takes priority over them, so after changing a default, either make the same change in `sdkconfig` or delete it to regenerate it.

### Over serial

A normal `idf.py -p <PORT> flash` over the UART adapter still works (see [Hardware setup for flashing](#hardware-setup-for-flashing)).

### Over the air (Matter OTA over Thread)

The build also produces `build/thermostat-ota.bin`, a Matter OTA image. Apple Home and Google Home only update certified devices, so you serve it yourself with the Matter SDK's host tools. Build them from the SDK with `./scripts/build/build_examples.py --target darwin-arm64-chip-tool --target darwin-arm64-ota-provider build`.

One-time setup: in the Home app, turn on pairing mode for the AC, then add the provider and the AC to `chip-tool`'s fabric. This makes `chip-tool` another admin on the AC, which uses one of its 5 fabric slots:

```bash
chip-tool pairing code 2 <setup-code-from-home-app>              # AC = node 2
chip-ota-provider-app -f build/thermostat-ota.bin --discriminator 3841 --secured-device-port 5565 &
chip-tool pairing onnetwork-long 1 20202021 3841                 # provider = node 1
chip-tool accesscontrol write acl '[{"fabricIndex": 1, "privilege": 5, "authMode": 2, "subjects": [112233], "targets": null}, {"fabricIndex": 1, "privilege": 3, "authMode": 2, "subjects": null, "targets": [{"cluster": 41, "endpoint": null, "deviceType": null}]}]' 1 0
```

Each update (with the provider running and serving the new `thermostat-ota.bin`):

```bash
chip-tool otasoftwareupdaterequestor announce-otaprovider 1 0 2 0 2 0
chip-tool basicinformation read software-version 2 0              # afterwards: the new number
```

The roughly 1.7 MB image takes about a quarter of an hour over Thread. The AC then restarts into the new version and keeps it once it has run for a minute while attached to the Thread network. If it doesn't get there within 5 minutes, or crashes before that, the bootloader automatically returns to the previous version.

Give your home apps a few minutes to reconnect after the restart, and don't poll the AC with repeated `chip-tool` commands. Each one opens a new secure session, and while the AC is setting one up it answers others with "busy", which can show up as "No Response" in your home app.

Rollback needs a bootloader built with rollback enabled (this build, and earlier builds since energy monitoring was added), and the bootloader can only be written over serial. A device with an older bootloader still updates fine over the air, it just won't roll back.

### Image size

Each OTA slot is 1.875 MB (`0x1E0000` in `partitions.csv`), and `idf.py build` prints how much room is left. An update can't change the partition layout, so growing the slots means reflashing every device over serial. Matter clusters this device doesn't create are left out to save space (see `sdkconfig.defaults`). To use one, delete its line there and delete `sdkconfig` so it regenerates. If it's still missing, the build stops with an undefined `Matter...PluginServerInitCallback`.

---

## Factory Reset

Hold the onboard reset button (GPIO23 → GND) for **at least 5 seconds, then release it**. The device will clear all fabrics and re-enter BLE commissioning mode. Make sure to also remove the device from whatever ecosystems you've added it to — controllers cache credentials and will refuse re-pairing otherwise.

Faikout Matter has no Matter reset button. Remove the device in your home app instead.

---

## Known Issues / Roadmap
- [x] **Energy reporting** — live power (W) + lifetime imported energy (kWh) via the Matter Electrical Power/Energy Measurement clusters, on S21 units that report it. Home Assistant shows both; the cooling/heating split is read but only logged (Matter carries a single imported/exported energy value, not a per-mode breakdown).
- [ ] **Swing / louver position** — the S21 protocol supports it; no Matter wiring yet.
- [ ] **Econo, comfort, streamer and other S21 settings** — not exposed over Matter in the original firmware yet. Toggling Powerful leaves them as they are. Faikout Matter can expose them as opt-in switches.
- [x] **X50A & CN_WIRED protocols (experimental)** — drivers for both are built in, with boot-time auto-detection (S21 → X50A → CN_WIRED). **Only S21 is hardware-verified.** X50A rides the same 2-wire UART front-end and should work with the right cable; CN_WIRED is a single-wire pulse protocol that needs a different connector/pull-up. RevK's wiring wiki notes that the CN_WIRED power pin can be above 12V without giving a figure, so measure it first: the included PCB is limited to 17V. Toggle them via `AC_ENABLE_X50A` / `AC_ENABLE_CNWIRED` in `Thermostat_Daikin/main/app_driver.cpp`.
- [ ] **Faikout Matter** — test with a real AC.
- [x] **Faikout Matter pairing** — a QR code, its own pairing code for each board, a settable name, and a button to turn Matter off.
- [ ] **Pre-built images** — test everything except `thread-c6` on real hardware.

---

## Customizing the device name

To change the manufacturer and model shown in your home app, edit [`main/CHIPProjectConfig.h`](Thermostat_Daikin/main/CHIPProjectConfig.h) before building:

```c
#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME   "Daikin"
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME  "Daikin A/C"
```

Apple Home, Google Home, and Alexa display these in the device's settings page. The device's own name is whatever you call it in your home app. Changing them after the device is already commissioned won't update existing controllers — you'll need to factory reset and re-commission.

For Faikout Matter, the same settings are in [`Faikout_Matter/main/CHIPProjectConfig.h`](Faikout_Matter/main/CHIPProjectConfig.h).

---

## License

GPL v3 — see [LICENSE](LICENSE)

The S21 protocol implementation is derived from the [Faikout](https://codeberg.org/RevK/ESP32-Faikout) project by RevK, which is licensed under GPL v3 — that's why both firmwares are also GPL v3. The original firmware is combined with portions of the [ESP-Matter](https://github.com/espressif/esp-matter) thermostat example by Espressif Systems (Apache 2.0, which is GPL-compatible in this direction).

[Faikout Matter](Faikout_Matter/README.md) contains modified copies of Faikout and its framework. Its README lists the upstream versions and the changes.

---

## Matter and Thread Certification

Neither firmware is Matter Certified or Thread Certified. Both implement the Matter protocol via Espressif's [esp-matter](https://github.com/espressif/esp-matter) SDK, and the original firmware's Thread images use OpenThread, but neither has been submitted to the Connectivity Standards Alliance (CSA) or Thread Group for certification. Certification requires CSA membership ($7,000+/year) and Authorized Test Lab evaluation, which are out of scope for an open-source community project.

Devices flashed with either firmware will appear in Matter ecosystems as **uncertified Matter devices**. Most ecosystems show a one-time warning during commissioning ("Add Anyway" in Apple Home); this is normal and expected.

Both firmwares use the publicly available ESP-Matter test VID/PID and setup code, which is acceptable for personal use and development but cannot be used in commercial certified products.

"Matter" is a trademark of the Connectivity Standards Alliance. "Thread" is a trademark of the Thread Group. "Daikin" is a trademark of Daikin Industries, Ltd. "Faikout" is a registered trademark of its owner. This project is not affiliated with, endorsed by, or sponsored by any of these organizations.

---

## Contributing

Contributions are welcome. This is a solo-maintained project, so response times will vary, but every issue and PR gets read.

### Testing without an AC

`Thermostat_Daikin/test/s21_sim/run.sh` runs the original firmware's real S21 driver against a simulated AC on your computer. It needs only a C++ compiler, not ESP-IDF or any hardware, and finishes instantly, so run it after changing `s21_driver.cpp`. The simulated AC is only a model of how a unit behaves, so still test on a real AC before relying on a change.

### Reporting bugs

Open a [GitHub issue](../../issues/new) with the details below. Report Faikout Matter problems here too, not to the Faikout project.

- **Firmware:** the original firmware (which image) or Faikout Matter
- **Hardware:** Board variant, S21 interface design (optocoupler / FET / which PCB), and Daikin model number
- **Ecosystem:** Apple Home, Google Home, Alexa, Home Assistant, etc., and any Thread Border Router(s) on your network
- **What happened:** What you expected, what actually occurred, and steps to reproduce
- **Logs:** Serial monitor output at 115200 baud, especially anything around the failure point

If your device commissions but the AC never updates state, the most likely cause is the S21 interface — wrong wiring, wrong connector type (Type A vs B), or the wrong GPIO pins compiled in (check [`main/app_driver.cpp`](Thermostat_Daikin/main/app_driver.cpp)). Verify by trying with the [included PCB](Custom%20PCB%20esp32c6/), which is known-good.

---

## Why?

Daikin makes excellent air conditioners and a famously poor cloud experience. The official path to remote control is the BRP-series WiFi adapter and the Daikin app, which:

- Costs €100+ for a module that's a glorified ESP32 + their cloud
- Routes every command through Daikin's servers
- Has no native Matter, HomeKit, or local API
- Stops working the moment Daikin retires the cloud product
- Doesn't extend to multiple ecosystems — it's the Daikin app or nothing

Meanwhile, most modern Daikin split units ship with an **S21 service port** sitting unused inside the indoor unit, exposing the full feature set of the AC over a documented(-ish) serial protocol. RevK's [Faikout](https://codeberg.org/RevK/ESP32-Faikout) project did the hard work of reverse-engineering the protocol and shipping the [Faikout](https://codeberg.org/RevK/ESP32-Faikout) hardware. This project bolts a Matter front-end onto that work — turning the same hardware into a fully local, cloud-free, multi-ecosystem Matter accessory that works directly with Apple Home, Google Home, Alexa, and Home Assistant. No app to install, no servers to depend on, no recurring fee.

### Why Matter over Thread?

Matter over Thread offers lower power consumption, lower latency, and a mesh that doesn't depend on your WiFi. Thread devices use any compatible Thread Border Router (HomePod mini, Apple TV 4K, iPhone 15 Pro+, Nest Hub 2nd gen, HA SkyConnect/Yellow) and continue to function even if your WiFi is down or congested. The ESP32-C6 has both a WiFi 6 radio and an 802.15.4 Thread radio — the Thread images use the Thread radio so the WiFi stack can stay quiet.

### Why Matter, and not Faikin / ESPHome / Tasmota?

Faikin is excellent and deserves credit — it's the source of this project's S21 implementation and the entire reason the protocol is documented at all. But Faikin (and ESPHome, Tasmota) all assume a central controller: Home Assistant, an MQTT broker, or a custom dashboard. They're great if that's your setup. They're a problem if it isn't.

Both firmwares here speak Matter directly. There's no middleware:

- **Apple Home user without Home Assistant?** It just works. Commission directly from the Home app.
- **Google Home or Alexa user?** Same — direct Matter commissioning, no third-party hub.
- **Home Assistant user?** Also works — commission via HA's Matter integration. Bonus: HA shows the fan slider whichever iOS version you have.
- **Multi-ecosystem household?** Multi-fabric commissioning lets the same device live in Apple Home, HA, and Google Home simultaneously.

Matter is an industry-standard protocol — your AC works with the ecosystems you have today and remains compatible with new Matter ecosystems as they appear.

If you like Faikout's web interface and MQTT, or already own a Faikin or Faikout board, [Faikout Matter](Faikout_Matter/README.md) means you don't have to choose: it keeps what Faikout does and adds Matter.

---
