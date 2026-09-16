#include "AppSettings.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr std::size_t MaximumLineLength = 160;

enum class Section { None, Wifi, Market, Other };

enum SettingFlag : std::uint16_t {
  WifiSsid = 1U << 0,
  WifiPassword = 1U << 1,
  ApiKey = 1U << 2,
  SymbolA = 1U << 3,
  SymbolB = 1U << 4,
  SymbolC = 1U << 5,
  RefreshMinutes = 1U << 6,
};

constexpr std::uint16_t RequiredSettings =
    WifiSsid | WifiPassword | ApiKey | SymbolA | SymbolB | SymbolC |
    RefreshMinutes;

SettingsParseResult failure(const char* message) {
  SettingsParseResult result{};
  std::snprintf(result.message, sizeof(result.message), "%s", message);
  return result;
}

char* trim(char* text) {
  while (*text != '\0' && std::isspace(static_cast<unsigned char>(*text))) {
    ++text;
  }

  char* end = text + std::strlen(text);
  while (end > text &&
         std::isspace(static_cast<unsigned char>(*(end - 1)))) {
    --end;
  }
  *end = '\0';
  return text;
}

template <std::size_t Size>
bool copyValue(char (&destination)[Size], const char* value) {
  const std::size_t length = std::strlen(value);
  if (length == 0 || length >= Size) {
    return false;
  }
  std::memcpy(destination, value, length + 1);
  return true;
}

bool copySymbol(char (&destination)[13], const char* value) {
  if (!copyValue(destination, value)) {
    return false;
  }

  for (char* character = destination; *character != '\0'; ++character) {
    const unsigned char current = static_cast<unsigned char>(*character);
    if (!std::isalnum(current) && *character != '.' && *character != '-') {
      return false;
    }
    *character = static_cast<char>(std::toupper(current));
  }
  return true;
}

bool parseRefreshMinutes(const char* value, std::uint32_t& intervalMs) {
  char* end = nullptr;
  const unsigned long minutes = std::strtoul(value, &end, 10);
  if (end == value || *end != '\0' || minutes < 1 || minutes > 1440) {
    return false;
  }
  intervalMs = static_cast<std::uint32_t>(minutes) * 60UL * 1000UL;
  return true;
}

}  // namespace

SettingsParseResult parseAppSettings(const char* input, std::size_t inputSize,
                                     AppSettings& settings) {
  if (input == nullptr || inputSize == 0 ||
      inputSize > MaximumSettingsFileSize ||
      std::memchr(input, '\0', inputSize) != nullptr) {
    return failure("Invalid settings.ini");
  }

  AppSettings parsed{};
  Section section = Section::None;
  std::uint16_t foundSettings = 0;
  std::size_t offset = 0;

  while (offset < inputSize) {
    const std::size_t lineStart = offset;
    while (offset < inputSize && input[offset] != '\n') {
      ++offset;
    }
    const std::size_t lineLength = offset - lineStart;
    if (offset < inputSize) {
      ++offset;
    }
    if (lineLength > MaximumLineLength) {
      return failure("settings.ini line too long");
    }

    char line[MaximumLineLength + 1]{};
    std::memcpy(line, input + lineStart, lineLength);
    char* content = trim(line);
    if (*content == '\0' || *content == ';' || *content == '#') {
      continue;
    }

    const std::size_t contentLength = std::strlen(content);
    if (content[0] == '[' && contentLength > 2 &&
        content[contentLength - 1] == ']') {
      content[contentLength - 1] = '\0';
      const char* sectionName = trim(content + 1);
      section = std::strcmp(sectionName, "wifi") == 0
                    ? Section::Wifi
                    : std::strcmp(sectionName, "market") == 0
                          ? Section::Market
                          : Section::Other;
      continue;
    }

    char* separator = std::strchr(content, '=');
    if (separator == nullptr) {
      return failure("Invalid settings.ini line");
    }
    *separator = '\0';
    const char* key = trim(content);
    const char* value = trim(separator + 1);

    if (section == Section::Wifi && std::strcmp(key, "ssid") == 0) {
      if (!copyValue(parsed.wifiSsid, value)) {
        return failure("Invalid Wi-Fi SSID");
      }
      foundSettings |= WifiSsid;
    } else if (section == Section::Wifi &&
               std::strcmp(key, "password") == 0) {
      if (!copyValue(parsed.wifiPassword, value)) {
        return failure("Invalid Wi-Fi password");
      }
      foundSettings |= WifiPassword;
    } else if (section == Section::Market &&
               std::strcmp(key, "api_key") == 0) {
      if (!copyValue(parsed.apiKey, value)) {
        return failure("Invalid API key");
      }
      foundSettings |= ApiKey;
    } else if (section == Section::Market &&
               std::strncmp(key, "symbol_", 7) == 0 &&
               key[7] >= 'a' && key[7] <= 'c' && key[8] == '\0') {
      const std::size_t symbolIndex = static_cast<std::size_t>(key[7] - 'a');
      if (!copySymbol(parsed.symbols[symbolIndex], value)) {
        return failure("Invalid stock symbol");
      }
      foundSettings |= static_cast<std::uint16_t>(SymbolA << symbolIndex);
    } else if (section == Section::Market &&
               std::strcmp(key, "refresh_minutes") == 0) {
      if (!parseRefreshMinutes(value, parsed.refreshIntervalMs)) {
        return failure("Invalid refresh interval");
      }
      foundSettings |= RefreshMinutes;
    }
  }

  if ((foundSettings & RequiredSettings) != RequiredSettings) {
    return failure("settings.ini incomplete");
  }

  settings = parsed;
  SettingsParseResult result{};
  result.status = SettingsParseStatus::Success;
  std::snprintf(result.message, sizeof(result.message), "Settings loaded");
  return result;
}