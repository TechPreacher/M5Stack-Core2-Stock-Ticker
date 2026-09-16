#pragma once

#include <cstddef>
#include <cstdint>

inline constexpr std::size_t SymbolCount = 3;
inline constexpr std::size_t MaximumSettingsFileSize = 2048;

struct AppSettings {
  char wifiSsid[33]{};
  char wifiPassword[65]{};
  char apiKey[65]{};
  char symbols[SymbolCount][13]{};
  std::uint32_t refreshIntervalMs = 30UL * 60UL * 1000UL;
};

enum class SettingsParseStatus { Success, Invalid };

struct SettingsParseResult {
  SettingsParseStatus status = SettingsParseStatus::Invalid;
  char message[80]{};
};

SettingsParseResult parseAppSettings(const char* input, std::size_t inputSize,
                                     AppSettings& settings);