# portable-mp3-player

A portable MP3 player built on the ESP32 using ESP-IDF. Music files are stored on a microSD card, which the ESP32 reads over SPI.

## Current status

The project is in its early stages. The firmware in [main/main.c](main/main.c) is currently an **SD card bring-up test**. It:

1. Waits 250 ms for the SD card's power to settle
2. Initializes the SPI bus (`SPI2_HOST`, also known as HSPI) and enables the internal pull-ups on MISO, MOSI and CS
3. Mounts the card's FAT filesystem at `/sdcard`
4. Prints the card info (name, type, speed, size) and lists the files in the root directory
5. Unmounts the card and frees the SPI bus

MP3 decoding and audio output are not implemented yet.

## Hardware

- ESP32 dev board (classic ESP32 target, 2 MB flash or more)
- SPI microSD card reader module
- microSD card formatted as FAT32

### SD card reader wiring

| SD card module pin | ESP32 GPIO | Notes                                |
|--------------------|------------|--------------------------------------|
| CS (SS)            | GPIO 5     | Chip select                          |
| SCK (CLK)          | GPIO 18    | SPI clock                            |
| MOSI (DI)          | GPIO 23    | ESP32 → card                         |
| MISO (DO)          | GPIO 19    | Card → ESP32                         |
| VCC                | 3V3 or 5V  | Check your module (see below)        |
| GND                | GND        | Use a common ground with the ESP32   |

These are the ESP32's default VSPI pins. They are defined at the top of [main/main.c](main/main.c) (`PIN_NUM_*`) if you need to change them.

**Power:** Many SD card modules have an onboard 3.3 V regulator and level shifter and must be fed **5 V**. Bare modules without a regulator must be fed **3.3 V**. Feeding 5 V to a module without a regulator can damage the card.

## Software setup

### Prerequisites

- [ESP-IDF v5.4.x](https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32/get-started/index.html) (the project was created with v5.4.1)
- A USB cable and the USB-to-UART driver for your dev board (usually CP210x or CH340)

### Build and flash

From an ESP-IDF terminal (or after running `export.sh` / `export.ps1`):

```sh
idf.py set-target esp32   # only needed once, sdkconfig already targets esp32
idf.py build
idf.py -p <PORT> flash monitor
```

Replace `<PORT>` with your board's serial port (for example `COM3` on Windows (from Device ) or `/dev/ttyUSB0` on Linux). Press `Ctrl+]` to exit the monitor.

### Preparing the SD card

- Format the card as **FAT32**. The firmware does not format the card if mounting fails (`format_if_mount_failed = false`).
- Long file names are disabled (`CONFIG_FATFS_LFN_NONE`), so files show up as 8.3 names (for example `SONG01.MP3`). Keep file names short, or enable long file names in `idf.py menuconfig` → *Component config* → *FAT Filesystem support*.

### Expected output

On success the serial monitor should show something like:

```
I (...) SD_TEST: Filesystem mounted successfully!
Name: SD16G
Type: SDHC/SDXC
Speed: 4.00 MHz (limit: 4.00 MHz)
Size: 14910MB
...
I (...) SD_TEST:  Found file: SONG01.MP3
I (...) SD_TEST: Card unmounted.
```

## Troubleshooting

| Symptom | Things to try |
|---------|---------------|
| `ESP_ERR_TIMEOUT` (0x107) or card not detected | Check the wiring and power, make sure the card is fully inserted, and try a different card. |
| `ESP_ERR_INVALID_CRC` (0x109) | The SPI clock is too fast for your wiring. Lower `host.max_freq_khz` in [main/main.c](main/main.c) from `4000` to `1000` or `400`. Keep the jumper wires short. |
| `ESP_FAIL` while mounting | The card is probably not FAT formatted. Reformat it as FAT32. |
| Random or unstable failures | Add external 10 kΩ pull-up resistors on MISO, MOSI and CS, and a 10–100 µF capacitor across the module's VCC and GND. |

## Project structure

```
.
├── CMakeLists.txt        # Top-level ESP-IDF project file
├── sdkconfig.defaults    # Default config overrides (SDIO support disabled)
├── sdkconfig             # Generated project configuration
└── main/
    ├── CMakeLists.txt    # Component registration (requires fatfs, sdmmc, esp_driver_spi)
    └── main.c            # Application entry point (SD card test)
```
