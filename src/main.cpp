#ifndef UNIT_TEST

#include <Arduino.h>
#include <M5Unified.h>
#include <time.h>

#include "AppSettings.generated.h"
#include "MarketDataClient.h"
#include "StockRenderer.h"
#include "WifiController.h"

namespace {

constexpr time_t MinimumValidTime = 1704067200;
constexpr std::uint32_t ClockWaitWarningMs = 20000;
constexpr std::uint32_t FailedFetchRetryMs = 60000;
constexpr std::uint32_t FreeTierRefreshIntervalMs = 65UL * 60UL * 1000UL;
constexpr std::uint32_t MarketTaskStackBytes = 32UL * 1024UL;

WifiController wifiController;
StockRenderer renderer;
QueueHandle_t resultQueue = nullptr;
TaskHandle_t marketTaskHandle = nullptr;
stock::Series currentSeries{};
bool hasData = false;
bool fetchInProgress = false;
bool clockRequested = false;
bool clockReady = false;
std::uint32_t clockRequestedAt = 0;
std::uint32_t lastFetchStartedAt = 0;
std::uint32_t fetchIntervalMs = AppSettings::RefreshIntervalMs;
WifiState displayedWifiState = WifiState::WaitingToRetry;

void marketTask(void*) {
  MarketDataClient client;
  static FetchResult result{};
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    result = client.fetch(AppSettings::Symbol, AppSettings::ApiKey);
    xQueueOverwrite(resultQueue, &result);
    Serial.printf("Market task stack minimum free: %u bytes\n",
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  }
}

void show(const char* status, bool stale = false) {
  renderer.render(AppSettings::Symbol, hasData ? &currentSeries : nullptr, status,
                  stale);
}

void startFetch() {
  if (fetchInProgress || marketTaskHandle == nullptr) {
    return;
  }
  fetchInProgress = true;
  lastFetchStartedAt = millis();
  show("Updating", hasData);
  xTaskNotifyGive(marketTaskHandle);
}

void updateWifiDisplay() {
  const WifiState state = wifiController.state();
  if (state == displayedWifiState) {
    return;
  }
  displayedWifiState = state;

  if (state == WifiState::Connected) {
    show(clockReady ? "Online" : "Syncing clock", hasData);
    return;
  }

  clockRequested = false;
  clockReady = false;
  if (state == WifiState::Connecting) {
    show("Connecting", hasData);
  } else {
    show("Wi-Fi retry", hasData);
  }
}

void updateClockAndFetch() {
  if (wifiController.state() != WifiState::Connected) {
    return;
  }

  if (!clockRequested) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    clockRequested = true;
    clockRequestedAt = millis();
    show("Syncing clock", hasData);
  }

  if (!clockReady) {
    const time_t now = time(nullptr);
    if (now >= MinimumValidTime) {
      clockReady = true;
      startFetch();
    } else if (millis() - clockRequestedAt >= ClockWaitWarningMs) {
      show("Clock unavailable", hasData);
    }
    return;
  }

  if (!fetchInProgress &&
      millis() - lastFetchStartedAt >= fetchIntervalMs) {
    startFetch();
  }
}

void receiveMarketResult() {
  if (resultQueue == nullptr) {
    return;
  }

  static FetchResult result{};
  if (xQueueReceive(resultQueue, &result, 0) != pdTRUE) {
    return;
  }

  fetchInProgress = false;
  if (result.status == FetchStatus::Success) {
    currentSeries = result.series;
    hasData = true;
    if (result.interval == MarketInterval::Daily) {
      fetchIntervalMs = max(AppSettings::RefreshIntervalMs, FreeTierRefreshIntervalMs);
      show("Daily close", false);
    } else {
      fetchIntervalMs = AppSettings::RefreshIntervalMs;
      show("Hourly", false);
    }
  } else {
    fetchIntervalMs = min(AppSettings::RefreshIntervalMs, FailedFetchRetryMs);
    lastFetchStartedAt = millis();
    show(result.message, hasData);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  auto config = M5.config();
  M5.begin(config);
  renderer.begin();
  show("Starting");

  resultQueue = xQueueCreate(1, sizeof(FetchResult));
  if (resultQueue == nullptr ||
      xTaskCreatePinnedToCore(marketTask, "market-data", MarketTaskStackBytes, nullptr, 1,
                              &marketTaskHandle, 0) != pdPASS) {
    marketTaskHandle = nullptr;
    show("Worker unavailable");
  }

  wifiController.begin(AppSettings::WifiSsid, AppSettings::WifiPassword);
  displayedWifiState = WifiState::WaitingToRetry;
  updateWifiDisplay();
}

void loop() {
  M5.update();
  wifiController.update();
  updateWifiDisplay();
  receiveMarketResult();
  updateClockAndFetch();

  if (clockReady && !fetchInProgress && M5.BtnA.wasClicked()) {
    startFetch();
  }
  delay(5);
}

#endif