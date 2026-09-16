#include "StockRenderer.h"

#include <algorithm>
#include <cstdio>

namespace {

constexpr int ScreenWidth = 320;
constexpr int ScreenHeight = 240;
constexpr int GraphLeft = 14;
constexpr int GraphTop = 100;
constexpr int GraphWidth = 292;
constexpr int GraphHeight = 96;
constexpr int ButtonBarTop = 218;
constexpr int ButtonBarHeight = ScreenHeight - ButtonBarTop;
constexpr int BatteryAreaLeft = 230;
constexpr int BatteryLeft = 244;
constexpr int BatteryTop = 8;
constexpr int BatteryWidth = 20;
constexpr int BatteryHeight = 12;
constexpr std::uint16_t Background = 0x0841;
constexpr std::uint16_t HeaderBackground = 0x18E3;
constexpr std::uint16_t GridColor = 0x4208;
constexpr std::uint16_t FlatColor = TFT_LIGHTGREY;

}  // namespace

StockRenderer::StockRenderer() : canvas_(&M5.Display) {}

bool StockRenderer::begin() {
  M5.Display.setRotation(1);
  M5.Display.setBrightness(160);
  canvas_.setColorDepth(16);
  ready_ = canvas_.createSprite(ScreenWidth, ScreenHeight) != nullptr;
  if (!ready_) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_RED);
    M5.Display.drawString("Display buffer failed", 12, 12);
  }
  return ready_;
}

void StockRenderer::setBatteryStatus(std::int32_t level, bool charging) {
  const std::int32_t normalizedLevel =
      level < 0 ? -1 : std::min<std::int32_t>(level, 100);
  if (normalizedLevel == batteryLevel_ && charging == batteryCharging_) {
    return;
  }

  batteryLevel_ = normalizedLevel;
  batteryCharging_ = charging;
  if (!ready_ || !frameRendered_) {
    return;
  }

  canvas_.fillRect(BatteryAreaLeft, 0, ScreenWidth - BatteryAreaLeft, 30,
                   HeaderBackground);
  drawBatteryStatus();
  canvas_.pushSprite(0, 0);
}

void StockRenderer::render(const char* symbol, const char* const* buttonSymbols,
                           std::size_t buttonCount, std::size_t selectedButton,
                           bool wifiConnected, const stock::Series* series,
                           const char* status, bool stale) {
  if (!ready_) {
    return;
  }

  char compactStatus[25]{};
  char detailStatus[47]{};
  char headerSymbol[9]{};
  std::snprintf(compactStatus, sizeof(compactStatus), "%.24s", status);
  std::snprintf(detailStatus, sizeof(detailStatus), "%.46s", status);
  std::snprintf(headerSymbol, sizeof(headerSymbol), "%.8s", symbol);

  canvas_.fillSprite(Background);
  canvas_.fillRect(0, 0, ScreenWidth, 30, HeaderBackground);
  canvas_.setTextDatum(textdatum_t::top_left);
  canvas_.setTextColor(TFT_WHITE);
  canvas_.setTextSize(2);
  canvas_.drawString(headerSymbol, 12, 7);

  if (stale) {
    canvas_.setTextDatum(textdatum_t::top_right);
    canvas_.setTextSize(1);
    canvas_.setTextColor(TFT_ORANGE);
    canvas_.drawString("STALE", BatteryAreaLeft - 5, 10);
  }
  drawWifiStatus(wifiConnected);
  drawBatteryStatus();
  drawSymbolLabels(buttonSymbols, buttonCount, selectedButton);

  if (series == nullptr || series->count == 0) {
    canvas_.setTextDatum(textdatum_t::middle_center);
    canvas_.setTextColor(TFT_WHITE);
    canvas_.setTextSize(2);
    canvas_.drawString(compactStatus, ScreenWidth / 2, ScreenHeight / 2 - 8);
    canvas_.setTextSize(1);
    canvas_.setTextColor(TFT_LIGHTGREY);
    canvas_.drawString("Waiting for market data", ScreenWidth / 2,
                       ScreenHeight / 2 + 20);
    frameRendered_ = true;
    canvas_.pushSprite(0, 0);
    return;
  }

  const stock::Trend trend = stock::calculateTrend(*series);
  const std::uint16_t trendColor = trend == stock::Trend::Up
                                       ? TFT_GREEN
                                       : trend == stock::Trend::Down ? TFT_RED
                                                                     : FlatColor;
  const float latest = series->points[series->count - 1].close;
  const float percentage = stock::calculatePercentageChange(*series);

  char priceText[24]{};
  std::snprintf(priceText, sizeof(priceText), "$%.2f", latest);
  canvas_.setTextDatum(textdatum_t::top_left);
  canvas_.setTextColor(TFT_WHITE);
  canvas_.setTextSize(3);
  canvas_.drawString(priceText, 12, 42);

  char changeText[24]{};
  std::snprintf(changeText, sizeof(changeText), "%+.2f%%", percentage);
  canvas_.setTextDatum(textdatum_t::top_right);
  canvas_.setTextColor(trendColor);
  canvas_.setTextSize(2);
  canvas_.drawString(changeText, ScreenWidth - 12, 50);

  canvas_.setTextSize(1);
  canvas_.setTextColor(stale ? TFT_ORANGE : TFT_LIGHTGREY);
  canvas_.drawString(detailStatus, ScreenWidth - 12, 82);

  drawGraph(*series, trendColor);

  canvas_.setTextSize(1);
  canvas_.setTextColor(TFT_LIGHTGREY);
  char firstDate[6]{};
  char lastDate[6]{};
  std::snprintf(firstDate, sizeof(firstDate), "%.5s", series->points[0].timestamp + 5);
  std::snprintf(lastDate, sizeof(lastDate), "%.5s",
                series->points[series->count - 1].timestamp + 5);
  canvas_.setTextDatum(textdatum_t::bottom_left);
  canvas_.drawString(firstDate, GraphLeft, ButtonBarTop - 3);
  canvas_.setTextDatum(textdatum_t::bottom_right);
  canvas_.drawString(lastDate, GraphLeft + GraphWidth, ButtonBarTop - 3);
  frameRendered_ = true;
  canvas_.pushSprite(0, 0);
}

void StockRenderer::drawBatteryStatus() {
  const std::uint16_t outlineColor = batteryCharging_ ? TFT_YELLOW : TFT_LIGHTGREY;
  canvas_.drawRect(BatteryLeft, BatteryTop, BatteryWidth, BatteryHeight, outlineColor);
  canvas_.fillRect(BatteryLeft + BatteryWidth, BatteryTop + 3, 3,
                   BatteryHeight - 6, outlineColor);

  if (batteryLevel_ >= 0) {
    const int fillWidth = static_cast<int>(
        (batteryLevel_ * (BatteryWidth - 4)) / 100);
    const std::uint16_t fillColor = batteryLevel_ <= 20 ? TFT_RED : TFT_GREEN;
    if (fillWidth > 0) {
      canvas_.fillRect(BatteryLeft + 2, BatteryTop + 2, fillWidth,
                       BatteryHeight - 4, fillColor);
    }
  }

  if (batteryCharging_) {
    canvas_.drawLine(BatteryLeft - 7, BatteryTop + 1, BatteryLeft - 10,
                     BatteryTop + 7, TFT_YELLOW);
    canvas_.drawLine(BatteryLeft - 10, BatteryTop + 7, BatteryLeft - 6,
                     BatteryTop + 7, TFT_YELLOW);
    canvas_.drawLine(BatteryLeft - 6, BatteryTop + 7, BatteryLeft - 9,
                     BatteryTop + BatteryHeight, TFT_YELLOW);
  }

  char levelText[6]{"--%"};
  if (batteryLevel_ >= 0) {
    std::snprintf(levelText, sizeof(levelText), "%ld%%",
                  static_cast<long>(batteryLevel_));
  }
  canvas_.setTextDatum(textdatum_t::top_right);
  canvas_.setTextSize(1);
  canvas_.setTextColor(outlineColor);
  canvas_.drawString(levelText, ScreenWidth - 8, 10);
}

void StockRenderer::drawWifiStatus(bool connected) {
  constexpr int CenterX = ScreenWidth / 2;
  const std::uint16_t signalColor = connected ? TFT_GREEN : TFT_LIGHTGREY;

  canvas_.drawLine(CenterX - 9, 11, CenterX - 6, 8, signalColor);
  canvas_.drawLine(CenterX - 6, 8, CenterX - 3, 6, signalColor);
  canvas_.drawLine(CenterX - 3, 6, CenterX, 5, signalColor);
  canvas_.drawLine(CenterX, 5, CenterX + 3, 6, signalColor);
  canvas_.drawLine(CenterX + 3, 6, CenterX + 6, 8, signalColor);
  canvas_.drawLine(CenterX + 6, 8, CenterX + 9, 11, signalColor);
  canvas_.drawLine(CenterX - 5, 14, CenterX - 2, 12, signalColor);
  canvas_.drawLine(CenterX - 2, 12, CenterX, 11, signalColor);
  canvas_.drawLine(CenterX, 11, CenterX + 2, 12, signalColor);
  canvas_.drawLine(CenterX + 2, 12, CenterX + 5, 14, signalColor);
  canvas_.fillCircle(CenterX, 18, 2, signalColor);

  if (!connected) {
    canvas_.drawLine(CenterX - 10, 5, CenterX + 10, 21, TFT_RED);
    canvas_.drawLine(CenterX - 10, 6, CenterX + 10, 22, TFT_RED);
  }
}

void StockRenderer::drawSymbolLabels(const char* const* symbols,
                                     std::size_t symbolCount,
                                     std::size_t selectedSymbol) {
  if (symbols == nullptr || symbolCount == 0) {
    return;
  }

  canvas_.drawFastHLine(0, ButtonBarTop, ScreenWidth, GridColor);
  for (std::size_t index = 0; index < symbolCount; ++index) {
    const int left = static_cast<int>((index * ScreenWidth) / symbolCount);
    const int right = static_cast<int>(((index + 1) * ScreenWidth) / symbolCount);
    if (index == selectedSymbol) {
      canvas_.fillRect(left, ButtonBarTop + 1, right - left, ButtonBarHeight - 1,
                       HeaderBackground);
    }
    if (index > 0) {
      canvas_.drawFastVLine(left, ButtonBarTop + 1, ButtonBarHeight - 1, GridColor);
    }

    char label[13]{};
    std::snprintf(label, sizeof(label), "%.12s", symbols[index]);
    canvas_.setTextDatum(textdatum_t::middle_center);
    canvas_.setTextSize(1);
    canvas_.setTextColor(index == selectedSymbol ? TFT_WHITE : TFT_LIGHTGREY);
    canvas_.drawString(label, (left + right) / 2,
                       ButtonBarTop + ButtonBarHeight / 2);
  }
}

void StockRenderer::drawGraph(const stock::Series& series, std::uint16_t color) {
  canvas_.drawRect(GraphLeft, GraphTop, GraphWidth, GraphHeight, GridColor);
  for (int division = 1; division < 4; ++division) {
    const int y = GraphTop + (GraphHeight * division) / 4;
    canvas_.drawFastHLine(GraphLeft + 1, y, GraphWidth - 2, GridColor);
  }

  if (series.count == 1) {
    canvas_.fillCircle(GraphLeft + GraphWidth / 2, GraphTop + GraphHeight / 2, 3,
                       color);
    return;
  }

  const stock::PriceRange range = stock::calculatePriceRange(series);
  const float span = range.maximum - range.minimum;
  int previousX = GraphLeft;
  int previousY = GraphTop + GraphHeight - 1 - static_cast<int>(
      ((series.points[0].close - range.minimum) / span) * (GraphHeight - 2));

  for (std::size_t index = 1; index < series.count; ++index) {
    const int x = GraphLeft + static_cast<int>(
                                (index * static_cast<std::size_t>(GraphWidth - 1)) /
                                (series.count - 1));
    const int y = GraphTop + GraphHeight - 1 - static_cast<int>(
        ((series.points[index].close - range.minimum) / span) * (GraphHeight - 2));
    canvas_.drawLine(previousX, previousY, x, y, color);
    canvas_.drawLine(previousX, previousY + 1, x, y + 1, color);
    previousX = x;
    previousY = y;
  }
}