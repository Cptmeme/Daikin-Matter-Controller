## Tools

Ideally if you want to do any work with the code or debug or flash devices you should install the [ESP IDF](https://www.espressif.com/en/products/sdks/esp-idf). This is a large package but includes tools like `idf.py monitor` to debug. However there are ways to access debug and flashing by other means including some web based tools (usually in Chrome).

## Debug

The serial interface (normally USB) allows debug output, you can access using one of

- `idf.py monitor` from ESP IDF
- `screen` command such as `screen /dev/ttyACM0`
- `esp-idf-monitor` from [here](https://github.com/espressif/esp-idf-monitor)
- Somewhere there is a web page that provides a monitor in Chrome without installing anything - but I can't find it.

Most of my designs have USB leads, but they also have a TC2030 port (see image on right) for a [TC2030-USB-NL](https://www.tag-connect.com/product/tc2030-usb-nl) lead.

## Flashing code

Normally applications can flash code themselves. The web interface has an *Install* option, and auto updates. You can also send an MQTT `command` to `update`.

However, if you want to change to different code or recover from some more serious failure you can re-flash code yourself.

- Download the `.bin` files from [OTA Server](https://ota.revk.uk) - this also has a `beta` subdirectory
- You can flash them using [https://adafruit.github.io/Adafruit_WebSerial_ESPTool/](https://adafruit.github.io/Adafruit_WebSerial_ESPTool/) from Chrome
- You can flash using `esptool` without having the whole of ESP IDF installed

The application files are also in the repository for the application, e.g. `MyApp`, in `release` and `release/beta` such as `MyApp-S3-MINI-N4-R2.bin` and maybe variants like `MyApp-S3-MINI-N4-R2-ETH.bin` if applicable.

A set of `Bootload-`, `Bootfast-`, `OTA-Data-`, and `Partition-` files are included in the [ESP32-Loader](https://codeberg.org/RevK/ESP32-Loader) repository for each chipset. The `Bootfast-` version is a fast startup for battery applications (see corresponding `.manifest` file).

Typically the code is flashed at these addresses, this is for ESP32-S3 modules. However for other builds you may need to find appropriate addresses. There may be a `.manifest` file in the `release` directory for your board which lists the files and addresses you will need as well as default settings.

|Offset|File|
|----|----|
|0x0|Bootload-S3-MINI-N4-R2.bin|
|0x8000|Partition-S3-MINI-N4-R2.bin|
|0xD000|OTA-Data-S3-MINI-N4-R2.bin|
|0x10000|MyApp-S3-MINI-N4-R2.bin|

Using `esptool` these would be done using 

```
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 460800 write_flash \
        0x0 Bootload-S3-MINI-N4-R2.bin \
        0x8000 Partition-S3-MINI-N4-R2.bin \
        0xD000 OTA-Data-S3-MINI-N4-R2.bin \
        0x10000 MyApp-S3-MINI-N4-R2.bin
```

In any case you may want to erase flash first to clear any settings.
