# ESP32 Inverter Controller

Firmware for an ESP32 that bridges an STM32-based inverter to the cloud. It
reads measurement data from the STM32 over a software serial link, connects to
WiFi, publishes data/status over MQTT, applies setup and schedule settings from
the backend API, and supports OTA (FOTA) firmware updates.

- **Board:** `esp32dev` (see `platformio.ini`)
- **Framework:** Arduino via PlatformIO
- **Main source:** `src/main.cpp`

## Prerequisites

- [PlatformIO Core (CLI)](https://docs.platformio.org/en/latest/core/installation/index.html)
  ```sh
  pip3 install platformio
  ```
- A C++ compiler (`g++`) for running the host unit tests.
- Python 3 for the flashing helper.

Build/upload dependencies are declared in `platformio.ini` and fetched
automatically by PlatformIO on first build.

## Build & upload manually

```sh
pio run                 # compile firmware
pio run -t upload       # compile + flash the connected device
pio device monitor      # open the serial monitor (9600 baud)
```

## Flashing devices with `flash_device.py`

Each device broadcasts a unique WiFi AP SSID of the form `GTIControl<N>`, defined
by `#define WIFI_BROADCAST_SSID "GTIControl<N>"` in `src/main.cpp`.
`flash_device.py` automates provisioning by **auto-incrementing that number**,
rebuilding, and uploading — so every unit you flash gets the next SSID in
sequence.

> ⚠️ The script **edits `src/main.cpp`** each run (it bumps the SSID number and
> saves the file). Commit or stash your work first if you don't want that change
> mixed in.

### Usage

```sh
# Flash 1 device (SSID = current number + 1)
python3 flash_device.py

# Flash multiple devices in one session
python3 flash_device.py 5
```

### How it works

1. Reads the current `GTIControl<N>` number from `src/main.cpp`.
2. For each device it sets the SSID to the next number, runs `pio run` to build,
   then `pio run -t upload` to flash the connected board.
3. When flashing more than one device, it **pauses between units** and prompts:
   `Connect device #<n> and press ENTER to continue...` — plug in the next
   board, then press ENTER.
4. Prints a summary with the SSID range that was flashed.

Example: if `main.cpp` currently has `GTIControl1171` and you run
`python3 flash_device.py 3`, the three devices are flashed as `GTIControl1172`,
`GTIControl1173`, and `GTIControl1174`.

The script locates the `pio` executable automatically (common install paths and
system `PATH`). If it can't find it, install the PlatformIO CLI as shown above.

## Running the tests

Host-side unit tests for the pure logic in `src/main.cpp` live in
`test/test_logic/`. They cover:

- `convertSetupValue()` — parsing an 8-digit setup string into the STM32 command
  format `*<Pset>@<Vset>#`.
- `isTimeInRange()` — schedule window checks, including overnight windows and
  24+ hour end times (e.g. `27:00` = `03:00` next day).

The functions run on your computer (no ESP32 needed) via a tiny Arduino `String`
shim.

### With g++ (fastest)

```sh
g++ -std=c++17 -o /tmp/test_logic test/test_logic/test_main.cpp && /tmp/test_logic
```

### With the PlatformIO test runner

```sh
pio test -e native
```

The run prints each check and exits non-zero if any test fails.

See `test/test_logic/README.md` for more detail.

## Project layout

```
src/main.cpp             Firmware entry point (setup/loop, WiFi, MQTT, OTA, serial)
platformio.ini           PlatformIO project + dependency configuration
flash_device.py          Multi-device flashing helper with auto-incrementing SSID
test/test_logic/         Host unit tests for the pure logic
include/  lib/            Project headers and private libraries
```
