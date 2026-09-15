---
title: M5Stack Core2 Stock Ticker
description: M5Stack Core2 firmware that displays three configurable stocks with five-trading-day graphs
---

## Features

* Maps the three Core2 buttons to configurable stock symbols
* Displays the selected symbol's latest hourly close and five-trading-day change
* Shows Wi-Fi connection state in the top-center header icon
* Draws green, red, or gray graph for rising, falling, or flat prices
* Connects to Wi-Fi from a build-time INI file
* Uses hourly Alpha Vantage data with premium keys
* Falls back to five daily closes when key lacks intraday entitlement
* Keeps last valid graph visible when Wi-Fi or API requests fail
* Runs HTTPS requests on a background FreeRTOS task to keep display loop responsive

## Prerequisites

* PlatformIO
* M5Stack Core2 connected over USB for upload and device tests
* Alpha Vantage API key; premium access enables hourly data

## Configuration

Copy [settings.ini.default](settings.ini.default) to `settings.ini`, then replace
placeholder values:

```ini
[wifi]
ssid = your-wifi-name
password = your-wifi-password

[market]
api_key = your-alpha-vantage-api-key
symbol_a = MSFT
symbol_b = AAPL
symbol_c = GOOGL
refresh_minutes = 30
```

`symbol_a`, `symbol_b`, and `symbol_c` map from left to right across the three
Core2 buttons. Symbols are converted to uppercase and may contain up to 12
letters, numbers, dots, or hyphens.

`settings.ini` is ignored by Git. PlatformIO validates it and generates a header
inside ignored `.pio` build output. Credentials are compiled into firmware, so
anyone with physical flash access may be able to extract them.

## Build And Upload

```bash
pio run -e m5stack-core2
pio run -e m5stack-core2 --target upload
pio device monitor --baud 115200
```

Press any Core2 button to select and fetch its labeled symbol. Pressing the
selected button requests an immediate refresh. Normal refreshes follow
`refresh_minutes`. Free daily fallback uses at least 65 minutes to remain below
Alpha Vantage's 25-request daily quota. Each button fetch counts toward that
quota. Failed requests retry after one minute.

## Validation

Compile firmware and embedded tests without hardware:

```bash
pio run -e m5stack-core2
pio test -e m5stack-core2 --without-uploading --without-testing
pio check -e m5stack-core2 --skip-packages
```

Run tests on a connected Core2:

```bash
pio test -e m5stack-core2
```

Device testing must confirm all three button mappings, Wi-Fi connection, NTP
synchronization, HTTPS trust, Alpha Vantage entitlement, graph rendering, and
recovery after network loss.

## Publish To GitHub

Initialize the local repository if this directory is not already a Git
repository:

```bash
git init
```

Before publishing, confirm Git ignores the local credentials file:

```bash
git check-ignore settings.ini
```

The command must print `settings.ini`. Never commit `settings.ini` or files from
the `.pio` build directory. Create the first commit:

```bash
git add .
git commit -m "Initial MS Stock display"
```

Authenticate GitHub CLI, then create and push a public repository:

```bash
gh auth login
gh repo create ms-stock-core2 --public --source=. --remote=origin --push
```

Replace `--public` with `--private` when source code should not be public.

## License

Licensed under the [MIT License](LICENSE).
