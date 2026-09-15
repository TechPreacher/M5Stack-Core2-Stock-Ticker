#pragma once

#include <Arduino.h>

enum class WifiState { Connecting, WaitingToRetry, Connected };

class WifiController {
 public:
  void begin(const char* ssid, const char* password);
  void update();

  WifiState state() const;
  std::uint32_t retrySecondsRemaining() const;

 private:
  void startConnection();
  void scheduleRetry();

  const char* ssid_ = nullptr;
  const char* password_ = nullptr;
  WifiState state_ = WifiState::WaitingToRetry;
  std::uint32_t connectionStartedAt_ = 0;
  std::uint32_t nextAttemptAt_ = 0;
  std::uint32_t retryDelayMs_ = 5000;
};