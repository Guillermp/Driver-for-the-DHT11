# Host unit tests

Run from `DHT11_sensor_driver` in a PlatformIO terminal:

```sh
pio test -e native
```

These Unity tests run on your computer without a sensor or board. The first run
installs PlatformIO's native platform and Unity dependency if needed.

`test_host/test_main.cpp` migrates the checks from `../main.cpp` into named tests.
It compiles and links the actual `src/decode.cpp` and `src/driver.cpp`; it does not
copy their implementations. The Arduino application's `src/main.cpp` is excluded
from the native build. Firmware builds still default to the ESP32 environment.

`test_host/mock_hardware.h` contains the original exercise's pin and clock mocks.
Each clock read advances simulated time by one microsecond. Handshake and data
are separate scenarios, not a complete sensor model or hardware timing validation.

Coverage includes checksum arithmetic, bit order, invalid pulses, full-frame
validation, measurement conversion, the shared data line, handshake, waits,
simulated decimal-zero data, stuck-line timeouts, and timer wraparound.
Floating-point comparisons use a tolerance. The decimal-zero mock deliberately
sends an incorrect checksum and tests that the decoded bytes remain accessible.

The original standalone exercise remains available for reference, but new tests
should be added here so they exercise the refactored sources. There is no test of
the complete `get_sensor_reading` transaction yet; the existing mock separates
handshake from data. Run real-board checks separately.

PlatformIO Unity documentation:
https://docs.platformio.org/en/stable/advanced/unit-testing/frameworks/unity.html
