#include "WifiController.h"

#include <WiFi.h>

namespace {

constexpr std::uint32_t ConnectionTimeoutMs = 15000;
constexpr std::uint32_t MaximumRetryDelayMs = 5UL * 60UL * 1000UL;

bool deadlineReached(std::uint32_t now, std::uint32_t deadline) {
  return static_cast<std::int32_t>(now - deadline) >= 0;
}

}  // namespace

void WifiController::begin(const char* ssid, const char* password) {
  ssid_ = ssid;
  password_ = password;
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  startConnection();
}

void WifiController::update() {
  const std::uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    if (state_ != WifiState::Connected) {
      state_ = WifiState::Connected;
      retryDelayMs_ = 5000;
    }
    return;
  }

  if (state_ == WifiState::Connected) {
    retryDelayMs_ = 5000;
    scheduleRetry();
    return;
  }

  if (state_ == WifiState::Connecting &&
      now - connectionStartedAt_ >= ConnectionTimeoutMs) {
    WiFi.disconnect(false, false);
    scheduleRetry();
    return;
  }

  if (state_ == WifiState::WaitingToRetry && deadlineReached(now, nextAttemptAt_)) {
    startConnection();
  }
}

WifiState WifiController::state() const { return state_; }

std::uint32_t WifiController::retrySecondsRemaining() const {
  if (state_ != WifiState::WaitingToRetry) {
    return 0;
  }
  const std::uint32_t now = millis();
  if (deadlineReached(now, nextAttemptAt_)) {
    return 0;
  }
  return (nextAttemptAt_ - now + 999) / 1000;
}

void WifiController::startConnection() {
  WiFi.begin(ssid_, password_);
  connectionStartedAt_ = millis();
  state_ = WifiState::Connecting;
}

void WifiController::scheduleRetry() {
  state_ = WifiState::WaitingToRetry;
  nextAttemptAt_ = millis() + retryDelayMs_;
  retryDelayMs_ = min(retryDelayMs_ * 2, MaximumRetryDelayMs);
}