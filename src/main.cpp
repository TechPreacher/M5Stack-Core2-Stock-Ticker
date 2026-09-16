#ifndef UNIT_TEST

#include <Arduino.h>
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>

#include "AppSettings.h"
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
constexpr std::uint32_t SdCardFrequencyHz = 25000000;
constexpr int SdCardChipSelect = 4;

static_assert(SymbolCount == 3, "Core2 requires three button symbols");

struct MarketRequest {
  std::size_t symbolIndex = 0;
  stock::ChartPeriod period = stock::ChartPeriod::Daily;
};

struct MarketResponse {
  std::size_t symbolIndex = 0;
  FetchResult fetch{};
};

WifiController wifiController;
StockRenderer renderer;
AppSettings appSettings{};
const char* symbolLabels[SymbolCount]{"", "", ""};
QueueHandle_t requestQueue = nullptr;
QueueHandle_t resultQueue = nullptr;
stock::Series currentSeries{};
std::size_t selectedSymbolIndex = 0;
std::size_t fetchingSymbolIndex = 0;
stock::ChartPeriod selectedPeriod = stock::ChartPeriod::Daily;
stock::ChartPeriod fetchingPeriod = stock::ChartPeriod::Daily;
bool hasData = false;
bool settingsReady = false;
bool fetchInProgress = false;
bool workerReady = false;
bool clockRequested = false;
bool clockReady = false;
std::uint32_t clockRequestedAt = 0;
std::uint32_t lastFetchStartedAt = 0;
std::uint32_t lastBatteryRefreshAt = 0;
std::uint32_t fetchIntervalMs = 30UL * 60UL * 1000UL;
WifiState displayedWifiState = WifiState::WaitingToRetry;
bool batteryStatusInitialized = false;

void marketTask(void*) {
  MarketDataClient client;
  static MarketRequest request{};
  static MarketResponse response{};
  while (true) {
    if (xQueueReceive(requestQueue, &request, portMAX_DELAY) != pdTRUE ||
        request.symbolIndex >= SymbolCount) {
      continue;
    }
    response.symbolIndex = request.symbolIndex;
    response.fetch = client.fetch(appSettings.symbols[request.symbolIndex],
                    appSettings.apiKey, request.period);
    xQueueOverwrite(resultQueue, &response);
    Serial.printf("Market task stack minimum free: %u bytes\n",
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  }
}

void show(const char* status, bool stale = false) {
  renderer.render(symbolLabels[selectedSymbolIndex], symbolLabels, SymbolCount,
                  selectedSymbolIndex,
                  wifiController.state() == WifiState::Connected,
                  hasData ? &currentSeries : nullptr, status, stale);
}

const char* chartPeriodStatus(stock::ChartPeriod period) {
  switch (period) {
    case stock::ChartPeriod::Daily:
      return "Daily chart";
    case stock::ChartPeriod::Weekly:
      return "Weekly chart";
    case stock::ChartPeriod::Monthly:
      return "Monthly chart";
  }
  return "Chart";
}

bool loadSettingsFromSdCard() {
  if (!SD.begin(SdCardChipSelect, SPI, SdCardFrequencyHz) ||
      SD.cardType() == CARD_NONE) {
    show("SD card missing");
    return false;
  }

  File settingsFile = SD.open("/settings.ini", FILE_READ);
  if (!settingsFile || settingsFile.isDirectory()) {
    show("settings.ini missing");
    return false;
  }

  const std::size_t fileSize = settingsFile.size();
  if (fileSize == 0 || fileSize > MaximumSettingsFileSize) {
    settingsFile.close();
    show("Invalid settings.ini");
    return false;
  }

  static char settingsBuffer[MaximumSettingsFileSize]{};
  const std::size_t bytesRead =
      settingsFile.readBytes(settingsBuffer, fileSize);
  settingsFile.close();
  if (bytesRead != fileSize) {
    show("SD read failed");
    return false;
  }

  const SettingsParseResult result =
      parseAppSettings(settingsBuffer, bytesRead, appSettings);
  if (result.status != SettingsParseStatus::Success) {
    show(result.message);
    return false;
  }

  for (std::size_t index = 0; index < SymbolCount; ++index) {
    symbolLabels[index] = appSettings.symbols[index];
  }
  fetchIntervalMs = appSettings.refreshIntervalMs;
  return true;
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

  const MarketRequest request{selectedSymbolIndex, selectedPeriod};
  if (xQueueOverwrite(requestQueue, &request) != pdPASS) {
    show("Worker unavailable", hasData);
    return;
  }
  fetchingSymbolIndex = selectedSymbolIndex;
  fetchingPeriod = selectedPeriod;
  fetchInProgress = true;
  lastFetchStartedAt = millis();
  show("Updating", hasData);
}

void selectSymbol(std::size_t symbolIndex) {
  if (symbolIndex >= SymbolCount) {
    return;
  }

  const bool changed = symbolIndex != selectedSymbolIndex;
  const stock::ChartPeriod nextPeriod = stock::chartPeriodAfterPress(
      selectedPeriod, !changed);
  selectedSymbolIndex = symbolIndex;
  if (changed || nextPeriod != selectedPeriod) {
    selectedPeriod = nextPeriod;
    currentSeries = {};
    hasData = false;
    fetchIntervalMs = appSettings.refreshIntervalMs;
  }

  if (!clockReady) {
    show(wifiController.state() == WifiState::Connected ? "Syncing clock"
                                                        : "Waiting for Wi-Fi");
  } else if (fetchInProgress) {
    show(selectedSymbolIndex == fetchingSymbolIndex &&
                 selectedPeriod == fetchingPeriod
             ? "Updating"
             : "Queued");
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
  if (response.symbolIndex != selectedSymbolIndex ||
      response.fetch.period != selectedPeriod) {
    startFetch();
    return;
  }

  const FetchResult& result = response.fetch;
  if (result.status == FetchStatus::Success) {
    currentSeries = result.series;
    hasData = true;
    if (result.interval == MarketInterval::Daily) {
      fetchIntervalMs = max(appSettings.refreshIntervalMs, FreeTierRefreshIntervalMs);
    } else {
      fetchIntervalMs = appSettings.refreshIntervalMs;
    }
    show(chartPeriodStatus(result.period), false);
  } else {
      fetchIntervalMs = result.status == FetchStatus::RateLimited
            ? max(appSettings.refreshIntervalMs,
              FreeTierRefreshIntervalMs)
            : min(appSettings.refreshIntervalMs,
              FailedFetchRetryMs);
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

  settingsReady = loadSettingsFromSdCard();
  if (!settingsReady) {
    return;
  }

  requestQueue = xQueueCreate(1, sizeof(MarketRequest));
  resultQueue = xQueueCreate(1, sizeof(MarketResponse));
  workerReady =
      requestQueue != nullptr && resultQueue != nullptr &&
      xTaskCreatePinnedToCore(marketTask, "market-data", MarketTaskStackBytes, nullptr,
                              1, nullptr, 0) == pdPASS;
  if (!workerReady) {
    show("Worker unavailable");
  }

  wifiController.begin(appSettings.wifiSsid, appSettings.wifiPassword);
  displayedWifiState = WifiState::WaitingToRetry;
  updateWifiDisplay();
}

void loop() {
  M5.update();
  updateBatteryDisplay();
  if (!settingsReady) {
    delay(50);
    return;
  }
  handleSymbolButtons();
  wifiController.update();
  updateWifiDisplay();
  receiveMarketResult();
  updateClockAndFetch();
  delay(5);
}

#endif