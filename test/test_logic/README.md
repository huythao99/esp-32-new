# Logic unit tests

Unit tests for the pure, hardware-independent helpers in `src/main.cpp`:

- `convertSetupValue()` — parses an 8-digit setup string into the STM32
  command format `*<Pset>@<Vset>#`.
- `isTimeInRange()` — schedule window check, including overnight windows and
  24+ hour end times (e.g. `27:00` = `03:00` next day).

The functions are copied into `test_main.cpp` alongside a tiny Arduino `String`
shim so the tests build and run on the host (no ESP32 hardware needed).

## Run

Standalone with g++ (fastest):

```sh
g++ -std=c++17 -o /tmp/test_logic test/test_logic/test_main.cpp && /tmp/test_logic
```

Or through the PlatformIO test runner:

```sh
pio test -e native
```

Exit code is non-zero if any check fails.
