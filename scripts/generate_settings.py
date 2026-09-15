"""Generate a C++ configuration header from the untracked settings.ini file."""

from __future__ import annotations

import configparser
import json
from pathlib import Path

Import("env")  # type: ignore[name-defined]  # noqa: F821


def require_value(config: configparser.ConfigParser, section: str, key: str) -> str:
    """Return a required INI value or stop the build with a useful error."""
    if not config.has_option(section, key):
        raise RuntimeError(f"settings.ini is missing [{section}] {key}")

    value = config.get(section, key).strip()
    if not value:
        raise RuntimeError(f"settings.ini has an empty [{section}] {key}")
    if "\n" in value or "\r" in value:
        raise RuntimeError(f"settings.ini [{section}] {key} must be one line")
    return value


project_dir = Path(env.subst("$PROJECT_DIR"))  # type: ignore[name-defined]  # noqa: F821
settings_path = project_dir / "settings.ini"
if not settings_path.is_file():
    raise RuntimeError(
        "settings.ini not found; copy settings.ini.default and add local credentials"
    )

config = configparser.ConfigParser(interpolation=None)
with settings_path.open(encoding="utf-8") as settings_file:
    config.read_file(settings_file)

ssid = require_value(config, "wifi", "ssid")
password = require_value(config, "wifi", "password")
api_key = require_value(config, "market", "api_key")
symbol = require_value(config, "market", "symbol").upper()

try:
    refresh_minutes = config.getint("market", "refresh_minutes")
except (configparser.Error, ValueError) as error:
    raise RuntimeError(
        "settings.ini [market] refresh_minutes must be an integer"
    ) from error

if not 1 <= refresh_minutes <= 1440:
    raise RuntimeError(
        "settings.ini [market] refresh_minutes must be between 1 and 1440"
    )

generated_dir = Path(env.subst("$BUILD_DIR")) / "generated"  # type: ignore[name-defined]  # noqa: F821
generated_dir.mkdir(parents=True, exist_ok=True)
header_path = generated_dir / "AppSettings.generated.h"
header = f"""#pragma once

#include <cstdint>

namespace AppSettings {{
inline constexpr char WifiSsid[] = {json.dumps(ssid)};
inline constexpr char WifiPassword[] = {json.dumps(password)};
inline constexpr char ApiKey[] = {json.dumps(api_key)};
inline constexpr char Symbol[] = {json.dumps(symbol)};
inline constexpr std::uint32_t RefreshIntervalMs = {refresh_minutes}UL * 60UL * 1000UL;
}}  // namespace AppSettings
"""
header_path.write_text(header, encoding="utf-8")
env.Append(CPPPATH=[str(generated_dir)])  # type: ignore[name-defined]  # noqa: F821