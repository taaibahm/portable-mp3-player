# portable-mp3-player

A portable MP3 player built on the ESP32 using ESP-IDF. Music files are stored on a microSD card, which the ESP32 reads over SPI.

## Current status

The project is in its early stages. The firmware currently runs three hardware tests at the same time, and all of them print to the ESP-IDF serial monitor. The tests don't depend on each other: the button and screen work without an SD card, and the SD test works without the screen.

### SD card test

The SD card bring-up test is in [main/main.c](main/main.c). It:

1. Waits 250 ms for the SD card's power to settle
2. Initializes the SPI bus (`SPI2_HOST`, also known as HSPI) and enables the internal pull-ups on MISO, MOSI and CS
3. Mounts the card's FAT filesystem at `/sdcard`
4. Prints the card info (name, type, speed, size) and lists the files in the root directory
5. Unmounts the card and frees the SPI bus

### Button test

The button input code is in [main/button.c](main/button.c). `app_main` registers the button on GPIO 25 before the SD test starts. The code:

1. Configures the pin as an input with the internal pull-up enabled
2. Fires an interrupt whenever the pin changes, in either direction
3. Waits 30 ms for the switch to stop bouncing, then logs **Button pressed** with a running count, or **Button released** with how long the button was held

The button is handled in its own FreeRTOS task, so it keeps working after the SD test finishes, even if the SD card fails to mount.

### OLED display test

The display driver is in [main/oled.c](main/oled.c). It talks to a 128x64 I2C OLED and draws text with a built-in 5x7 font: 21 characters per row, 8 rows.

At startup the screen shows "Press the button on GPIO 25". While the GPIO 25 button is held down, the screen shows "Button pressed!" and a press count. When you let go, it goes back to the start message.

MP3 decoding and audio output are not implemented yet.

## Hardware

- ESP32 dev board (classic ESP32 target, 2 MB flash or more)
- SPI microSD card reader module
- microSD card formatted as FAT32
- 1.3" 128x64 I2C OLED module, 4 pins. Ours is sold as SSD1306 but is actually an SH1106, which the driver is set up for by default
- Momentary push button
- Breadboard and jumper wires

### Pin overview

| ESP32 pin | Header (DevKit) | Connects to                         |
|-----------|-----------------|-------------------------------------|
| GPIO 5    | Right           | SD card CS                          |
| GPIO 18   | Right           | SD card SCK                         |
| GPIO 19   | Right           | SD card MISO                        |
| GPIO 23   | Right           | SD card MOSI                        |
| GPIO 32   | Left            | OLED SCL                            |
| GPIO 33   | Left            | OLED SDA                            |
| GPIO 25   | Left            | Button                              |
| 3V3       | Either          | OLED VCC, SD card VCC if it's 3.3 V |
| 5V / VIN  | Either          | SD card VCC if it's a 5 V module    |
| GND       | Either          | GND of every module and the button  |

"Left" and "Right" are for a standard ESP32 DevKit held with the USB port at the bottom and the chip facing you. Check the pin labels printed on your board, because layouts vary.

All GPIO numbers are defined at the top of [main/main.c](main/main.c) (`PIN_NUM_*`) if you need to change them.

### Power and ground

Every part has to share a ground with the ESP32, or nothing will talk to each other.

1. Run a jumper from an ESP32 **GND** pin to the breadboard's **blue (−) rail**.
2. Run a jumper from the ESP32 **3V3** pin to the breadboard's **red (+) rail**.
3. Connect every module's GND to the − rail. One leg of the button also goes to the − rail.
4. Connect the OLED's VCC to the + rail (3.3 V).
5. Connect the SD module's VCC to the + rail only if it's a 3.3 V module. A 5 V module goes to the ESP32's **5V** (or **VIN**) pin instead. Never connect 5V to the + rail.

### SD card reader wiring

| SD card module pin | ESP32 pin  | Notes                                |
|--------------------|------------|--------------------------------------|
| CS (SS)            | GPIO 5     | Chip select                          |
| SCK (CLK)          | GPIO 18    | SPI clock                            |
| MOSI (DI)          | GPIO 23    | ESP32 → card                         |
| MISO (DO)          | GPIO 19    | Card → ESP32                         |
| VCC                | 3V3 or 5V  | Check your module (see below)        |
| GND                | GND        |                                      |

These are the ESP32's default VSPI pins.

**Power:** Many SD card modules have an onboard 3.3 V regulator and level shifter and must be fed **5 V**. Bare modules without a regulator must be fed **3.3 V**. Feeding 5 V to a module without a regulator can damage the card.

### OLED display wiring

| OLED pin | ESP32 pin | Notes                                    |
|----------|-----------|------------------------------------------|
| VCC      | 3V3       | Use 3.3 V, not 5 V (see below)           |
| GND      | GND       |                                          |
| SCL      | GPIO 32   | I2C clock                                |
| SDA      | GPIO 33   | I2C data                                 |

The ESP32 can route I2C to almost any GPIO, so these were picked to keep the OLED on the opposite header from the SD card.

**Check the pin labels printed on your module.** Some modules have GND first and some have VCC first, and swapping them can damage the display.

The module works on 3.3 V or 5 V, but its onboard pull-up resistors connect SDA and SCL to VCC. Powering it from 3V3 keeps the I2C lines at a voltage that is safe for the ESP32.

The firmware checks I2C addresses `0x3C` and `0x3D` and uses whichever one answers.

### Button wiring

| Button leg | ESP32 pin | Notes                                   |
|------------|-----------|-----------------------------------------|
| Leg 1      | GPIO 25   | Internal pull-up enabled, active low    |
| Leg 2      | GND       |                                         |

No resistor is needed, because the internal pull-up is enabled and a pressed button reads as low. Don't connect the button to 3V3.

A four-leg tactile button has its legs joined in pairs inside. Put the button across the breadboard's center gap and use **two diagonally opposite legs**, so the button actually opens and closes the circuit.

If you want to use a different pin, avoid these:

- 5, 18, 19, 23, 25, 32 and 33, which the SD card, OLED and button already use
- 0, 2, 12 and 15, which are strapping pins
- 6–11, which are connected to the internal flash
- 34–39, which are input-only and have no internal pull-up

GPIO 4, 13, 14, 21, 22, 26 and 27 are free and safe to use.

### Wiring checklist

Check these before plugging in the USB cable:

- [ ] ESP32 GND → breadboard − rail, ESP32 3V3 → breadboard + rail
- [ ] SD card: CS → 5, SCK → 18, MOSI → 23, MISO → 19, VCC → 3V3 or 5V (depending on the module), GND → − rail
- [ ] OLED: VCC → + rail (3V3), GND → − rail, SCL → 32, SDA → 33, matched to the labels printed on the module
- [ ] Button: GPIO 25 ↔ GND, using diagonal legs
- [ ] Nothing is connected between 5V and the + rail

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
I (...) OLED: Display found at 0x3C
I (...) BUTTON: Button ready on GPIO 25
I (...) SD_TEST: Filesystem mounted successfully!
Name: SD16G
Type: SDHC/SDXC
Speed: 4.00 MHz (limit: 4.00 MHz)
Size: 14910MB
...
I (...) SD_TEST:  Found file: SONG01.MP3
I (...) SD_TEST: Card unmounted.
I (...) BUTTON: Button pressed (GPIO 25, count 1)
I (...) BUTTON: Button released (GPIO 25, held 184 ms)
```

The button lines show up whenever you press the button.

## Troubleshooting

| Symptom | Things to try |
|---------|---------------|
| `ESP_ERR_TIMEOUT` (0x107) or card not detected | Check the wiring and power, make sure the card is fully inserted, and try a different card. |
| `ESP_ERR_INVALID_CRC` (0x109) | The SPI clock is too fast for your wiring. Lower `host.max_freq_khz` in [main/main.c](main/main.c) from `4000` to `1000` or `400`. Keep the jumper wires short. |
| `ESP_FAIL` while mounting | The card is probably not FAT formatted. Reformat it as FAT32. |
| Random or unstable SD failures | Add external 10 kΩ pull-up resistors on MISO, MOSI and CS, and a 10–100 µF capacitor across the module's VCC and GND. |
| `No display found at 0x3C or 0x3D` | Check VCC and GND first, then check SDA and SCL. They're easy to swap, so try exchanging GPIO 32 and GPIO 33. |
| Left edge of the text is cut off, and there's a strip of noise on the right | The driver's column offset doesn't match your display's chip. The default `OLED_COL_OFFSET` of `2` in [main/oled.c](main/oled.c) is for SH1106 displays (most 1.3" modules). For a true SSD1306 (most 0.96" modules), set it to `0`. |
| Text is upside down or mirrored | Change `0xA1` to `0xA0` and `0xC8` to `0xC0` in `init_cmds` in [main/oled.c](main/oled.c). |
| Button press not logged | Check that the button connects its GPIO to **GND** (not 3V3) when pressed. Four-leg tactile buttons are internally joined in pairs, so use two legs on opposite corners. |
| One press logs several presses | The switch is bouncing for longer than 30 ms. Increase `DEBOUNCE_MS` in [main/button.c](main/button.c). |

## Project structure

```
.
├── CMakeLists.txt        # Top-level ESP-IDF project file
├── sdkconfig.defaults    # Default config overrides (SDIO support disabled)
├── sdkconfig             # Generated project configuration
└── main/
    ├── CMakeLists.txt    # Component registration
    ├── main.c            # Application entry point, pin definitions, SD card and OLED tests
    ├── button.c          # Button input (interrupt + debounce, logs presses, optional callback)
    ├── button.h          # button_add() and the callback type
    ├── oled.c            # SSD1306/SH1106 I2C OLED driver with a 5x7 text font
    └── oled.h            # OLED init, clear, draw text and flush functions
```
