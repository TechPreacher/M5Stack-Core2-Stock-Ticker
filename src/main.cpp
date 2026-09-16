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
constexpr std::uint32_t BatteryRefreshIntervalMs = 10000;
constexpr std::uint32_t MarketTaskStackBytes = 32UL * 1024UL;

static_assert(AppSettings::SymbolCount == 3, "Core2 requires three button symbols");

struct MarketRequest {
  std::size_t symbolIndex = 0;
};

struct MarketResponse {
  std::size_t symbolIndex = 0;
  FetchResult fetch{};
};

WifiController wifiController;
StockRenderer renderer;
QueueHandle_t requestQueue = nullptr;
QueueHandle_t resultQueue = nullptr;
stock::Series currentSeries{};
std::size_t selectedSymbolIndex = 0;
std::size_t fetchingSymbolIndex = 0;
bool hasData = false;
bool fetchInProgress = false;
bool workerReady = false;
bool clockRequested = false;
bool clockReady = false;
std::uint32_t clockRequestedAt = 0;
std::uint32_t lastFetchStartedAt = 0;
std::uint32_t lastBatteryRefreshAt = 0;
std::uint32_t fetchIntervalMs = AppSettings::RefreshIntervalMs;
WifiState displayedWifiState = WifiState::WaitingToRetry;
bool batteryStatusInitialized = false;

void marketTask(void*) {
  MarketDataClient client;
  static MarketRequest request{};
  static MarketResponse response{};
  while (true) {
    if (xQueueReceive(requestQueue, &request, portMAX_DELAY) != pdTRUE ||
        request.symbolIndex >= AppSettings::SymbolCount) {
      continue;
    }
    response.symbolIndex = request.symbolIndex;
    response.fetch =
        client.fetch(AppSettings::Symbols[request.symbolIndex], AppSettings::ApiKey);
    xQueueOverwrite(resultQueue, &response);
    Serial.printf("Market task stack minimum free: %u bytes\n",
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  }
}

void show(const char* status, bool stale = false) {
  renderer.render(AppSettings::Symbols[selectedSymbolIndex], AppSettings::Symbols,
                  AppSettings::SymbolCount, selectedSymbolIndex,
                  wifiController.state() == WifiState::Connected,
                  hasData ? &currentSeries : nullptr, status, stale);
}

void updateBatteryDisplay(bool force = false) {
  const std::uint32_t now = millis();
  if (!force && batteryStatusInitialized &&
      now - lastBatteryRefreshAt < BatteryRefreshIntervalMs) {
    return;
  }

  lastBatteryRefreshAt = now;
  batteryStatusInitialized = true;
  const bool charging =
      M5.Power.isCharging() == m5::Power_Class::is_charging;
  renderer.setBatteryStatus(M5.Power.getBatteryLevel(), charging);
}

void startFetch() {
  if (fetchInProgress || !workerReady) {
    return;
  }

  const MarketRequest request{selectedSymbolIndex};
  if (xQueueOverwrite(requestQueue, &request) != pdPASS) {
    show("Worker unavailable", hasData);
    return;
  }
  fetchingSymbolIndex = selectedSymbolIndex;
  fetchInProgress = true;
  lastFetchStartedAt = millis();
  show("Updating", hasData);
}

void selectSymbol(std::size_t symbolIndex) {
  if (symbolIndex >= AppSettings::SymbolCount) {
    return;
  }

  const bool changed = symbolIndex != selectedSymbolIndex;
  selectedSymbolIndex = symbolIndex;
  if (changed) {
    currentSeries = {};
    hasData = false;
    fetchIntervalMs = AppSettings::RefreshIntervalMs;
  }

  if (!clockReady) {
    show(wifiController.state() == WifiState::Connected ? "Syncing clock"
                                                        : "Waiting for Wi-Fi");
  } else if (fetchInProgress) {
    show(selectedSymbolIndex == fetchingSymbolIndex ? "Updating" : "Queued");
  } else {
    startFetch();
  }
}

void handleSymbolButtons() {
  if (M5.BtnA.wasClicked()) {
    selectSymbol(0);
  } else if (M5.BtnB.wasClicked()) {
    selectSymbol(1);
  } else if (M5.BtnC.wasClicked()) {
    selectSymbol(2);
  }
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

  static MarketResponse response{};
  if (xQueueReceive(resultQueue, &response, 0) != pdTRUE) {
    return;
  }

  fetchInProgress = false;
  if (response.symbolIndex != selectedSymbolIndex) {
    startFetch();
    return;
  }

  const FetchResult& result = response.fetch;
  if (result.status == FetchStatus::Success) {
    currentSeries = result.series;
    hasData = true;
    if (result.interval == MarketInterval::Daily) {
      fetchIntervalMs = max(AppSettings::RefreshIntervalMs, FreeTierRefreshIntervalMs);
      show("5-day change", false);
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
  updateBatteryDisplay(true);
  show("Starting");

  requestQueue = xQueueCreate(1, sizeof(MarketRequest));
  resultQueue = xQueueCreate(1, sizeof(MarketResponse));
  workerReady =
      requestQueue != nullptr && resultQueue != nullptr &&
      xTaskCreatePinnedToCore(marketTask, "market-data", MarketTaskStackBytes, nullptr,
                              1, nullptr, 0) == pdPASS;
  if (!workerReady) {
    show("Worker unavailable");
  }

  wifiController.begin(AppSettings::WifiSsid, AppSettings::WifiPassword);
  displayedWifiState = WifiState::WaitingToRetry;
  updateWifiDisplay();
}

void loop() {
  M5.update();
  updateBatteryDisplay();
  handleSymbolButtons();
  wifiController.update();
  updateWifiDisplay();
  receiveMarketResult();
  updateClockAndFetch();
  delay(5);
}

#endif