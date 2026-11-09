# ESP32 Sensor Demos

ESP-IDF examples for an ESP32 development board:

- **BME680 demo:** periodically prints temperature, humidity, pressure and gas resistance.
- **Peripheral hardware test:** runs a finite addressable-LED color sequence and PCF8574 output/readback test.
- **I2C diagnostics:** probes bus addresses and compares external-only and internal-assisted pull-ups without LED transmissions or PCF8574 output-data writes.

The committed `sdkconfig` selects the **BME680 demo** on the **ESP32**.

## Prerequisites

- ESP-IDF **v6.1**, its Python environment and ESP32 toolchain. This is the version verified with a fresh clone; other versions and targets have not been validated.
- Git and the build prerequisites described in the [ESP-IDF getting-started guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/).
- An ESP32 board and USB data cable for flashing and serial monitoring.
- The peripherals required by the selected demo.
- For host tests: Python 3 and a host C compiler with AddressSanitizer and UndefinedBehaviorSanitizer support, such as GCC.

## Clone the project

```bash
git clone https://github.com/bonshates/udemy_esp32_sensor_demos.git
cd udemy_esp32_sensor_demos
```

`esp-idf-lib/` is included directly, with local driver modifications. No submodule initialization or separate library download is needed.

## Activate ESP-IDF

Activate your ESP-IDF environment in each new terminal before running `idf.py`.

### Existing installation on this Ubuntu machine

The verified installation was created by Espressif Installation Manager. Use its generated activation script:

```bash
source "$HOME/.espressif/tools/activate_idf_v6.1.sh"
idf.py --version
```

### Other installations

Install ESP-IDF v6.1 and the ESP32 tools following Espressif's instructions. For a conventional installation, source `export.sh` from your actual ESP-IDF checkout, for example:

```bash
source "$HOME/esp/esp-idf/export.sh"
idf.py --version
```

If activation reports a missing Python environment or toolchain, use the activation script supplied by your installation manager, or finish the ESP-IDF installation. On the verified machine, the standard `export.sh` alone does not locate the installation-manager-managed tools correctly.

## Configure and build

Run these commands from the project root:

```bash
idf.py menuconfig
idf.py build
```

`menuconfig` is optional when using the committed configuration. Under **Example configuration**, choose the demo, sensor address and GPIO assignments.

The committed configuration uses:

- Target: `esp32`.
- Flash size: `2MB`; verify this matches your board before flashing.
- Mode: BME680 demo (`CONFIG_HARDWARE_TEST_ENABLED` disabled).
- BME680 I2C address: **0x77**; select **0x76** if your sensor uses that address.
- I2C SDA: **GPIO18**.
- I2C SCL: **GPIO19**.

The target is already configured. Use `idf.py set-target esp32` only when you deliberately need to reset the target configuration; it clears the build directory and regenerates `sdkconfig`.

Successful builds create `build/app-template.bin`, the bootloader and partition-table images. The firmware name comes from `project(app-template)` in the root `CMakeLists.txt`. Build output is ignored by Git; `sdkconfig` is tracked.

**Configuration reset note:** although the committed configuration disables hardware-test mode, its Kconfig default is enabled. If you regenerate `sdkconfig`, explicitly check the selected mode before flashing.

## Wiring and demo selection

### BME680 demo

Connect SDA to GPIO18, SCL to GPIO19 and a common ground, unless you changed the pins in `menuconfig`. Power the breakout according to its specifications and keep ESP32-facing I2C signals at **3.3 V**. Use external I2C pull-ups to 3.3 V, accounting for any pull-ups already fitted to the breakout.

Leave **Run LED strip and PCF8574 hardware tests instead of the BME680 demo** disabled. After boot, the application prints sensor readings periodically. A missing sensor, incorrect address or wiring problem can cause initialization to fail.

### LED-strip and PCF8574 hardware test

Enable **Run LED strip and PCF8574 hardware tests instead of the BME680 demo**, then configure **Peripheral hardware-test settings**.

Defaults when enabling this mode:

- LED data: GPIO23, one WS2812 RGB pixel, brightness 32/255.
- Other supported LED types: SK6812, APA106 and SM16703; enable RGBW only for four-byte pixels.
- PCF8574: address 0x20, using the configured I2C SDA/SCL pins.
- Ten test steps, approximately one second per step.

Use the strip's required power supply and any required data-level shifting; share ground with the ESP32. Do not power a strip from a GPIO.

PCF8574 P0 drives an **active-low test LED with a suitable series resistor**; P1 can connect to an optional button to ground. Other pins remain released. Supported address ranges are 0x20–0x27 and 0x38–0x3f.

**Connect test LEDs only, not relays, motors or other loads.** The test drives P0 low during alternating steps, then releases all expander pins and turns off the strip. It runs once per boot. Successful RMT transmission does not prove that a strip is attached: verify the colors visually.

### I2C diagnostic mode

Enable hardware-test mode, then enable **Only scan I2C addresses and compare pull-up configurations**.

This mode probes 7-bit addresses 0x08–0x77, samples digital idle levels and compares scans with internal pull-ups disabled and enabled. It skips LED transmissions and PCF8574 output-data writes. An ACK indicates an address response, not positive chip identification.

Internal pull-ups are a diagnostic aid, not a replacement for external pull-ups. Digital readings do not measure bus voltage, resistance or signal rise time.

## Flash and monitor

Replace `/dev/ttyUSB0` with your board's serial device, such as `/dev/ttyACM0`:

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Exit the monitor with **Ctrl+]**. If the serial device is unavailable, check the USB cable, port permissions and whether another program has opened it.

## Host tests

The modified LED driver includes a hardware-independent test:

```bash
python3 esp-idf-lib/components/led_strip/tests/test_host.py
```

It compiles the production LED driver against mocked SDK interfaces with `-Wall -Wextra -Werror` and address/undefined-behavior sanitizers. It checks timing configuration, RGBW handling, brightness, transmit snapshots, timeouts, cleanup and bounds. Set `CC` if a compiler other than `cc` is required.

These tests do not validate electrical wiring, real sensor communication or physical LED output.

## Project layout

- `main/main.c`: selects the application mode.
- `main/bme680_sensor.c`: BME680 measurement task.
- `main/hardware_test.c`: LED-strip and PCF8574 test sequence.
- `main/i2c_diagnostics.c`: address scanning and pull-up diagnostics.
- `main/Kconfig.projbuild`: application configuration options.
- `esp-idf-lib/components/`: bundled peripheral drivers and their dependencies.
- `sdkconfig`: committed ESP-IDF configuration.
- `.clangd`: editor configuration pointing to the original machine's build directory; update its compilation-database path when using a different checkout.

## Verification and known limitations

A fresh GitHub clone built successfully for ESP32 with ESP-IDF v6.1, and the LED-strip host test passed. This verification did not flash a board or confirm physical hardware operation.

The build emits legacy-I2C deprecation warnings. Those APIs are planned for removal in ESP-IDF v7.0, so driver migration is required before upgrading to that version.

## License

The application is public domain or CC0, at your option; see [LICENSE](LICENSE). Bundled third-party drivers retain their own copyright and license notices; consult the relevant files under `esp-idf-lib/`.
