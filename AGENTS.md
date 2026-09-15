---
title: MS Stock Agent Guidelines
description: Project-wide guidance for developing the M5Stack Core2 Microsoft stock display
---

## Project State

* Treat [src/main.cpp](src/main.cpp) as orchestration only. Wi-Fi, market-data,
  series transformation, and rendering behavior belongs in its existing component.
* Target only the `m5stack-core2` environment defined in
  [platformio.ini](platformio.ini): ESP32, Arduino framework, M5Unified, PSRAM,
  and a 16 MB partition layout.
* Manage firmware dependencies and build flags through `platformio.ini`.
* Follow [README.md](README.md) for local configuration and device operation.

## Product Contract

* Display Microsoft stock (`MSFT`) with its current price and a one-week price
  graph on the M5Stack Core2 screen.
* Render the graph green when the price is up over the displayed period and red
  when it is down.
* Connect to Wi-Fi using settings loaded from an INI file.
* Prefer Alpha Vantage premium intraday data with hourly regular-market points.
  Fall back to daily closes and a quota-safe 65-minute refresh for free keys.
* Use a five-trading-day window, USD, a 30-minute premium default refresh, and
  gray for flat data.
* Read Wi-Fi and market settings from untracked `settings.ini` at build time.

## Design Constraints

* Keep `setup()` for initialization and `loop()` responsive. Avoid long blocking
  delays during Wi-Fi connection, HTTP requests, retries, and display refreshes.
* Separate Wi-Fi configuration, market-data retrieval, response parsing,
  one-week series transformation, and rendering so pure logic can be tested
  independently of hardware.
* Design layouts for the Core2's 320 x 240 display. Reserve stable regions for
  current price, connection/error state, axes, and graph to prevent redraw jitter.
* Use HTTPS for market data. Bound response sizes, validate parsed values and
  timestamps, handle partial data, and preserve the last valid display when an
  update fails.
* Keep Wi-Fi and API secrets out of tracked `platformio.ini`, source files, test
  fixtures, serial logs, and commits. Store only a sanitized INI example in Git.
* Never log credentials or authorization headers. Reduce `CORE_DEBUG_LEVEL=5`
  before production use if logs could expose request details.

## Build And Validation

Run commands from the repository root:

```bash
pio run -e m5stack-core2
pio test -e m5stack-core2
pio check -e m5stack-core2
pio run -e m5stack-core2 --target upload
pio device monitor --baud 115200
```

* Run at least `pio run -e m5stack-core2` after firmware or dependency changes.
* Add focused tests under `test/` for parsing, trend calculation, graph scaling,
  malformed INI input, and incomplete market data when those components appear.
* Use `pio test -e m5stack-core2 --without-uploading --without-testing` to compile
  tests when Core2 hardware is unavailable.
* Report when upload, device tests, or display checks cannot run because Core2
  hardware or a serial port is unavailable.
