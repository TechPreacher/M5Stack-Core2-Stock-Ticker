#include "StockRenderer.h"

#include <algorithm>
#include <cstdio>

namespace {

constexpr int ScreenWidth = 320;
constexpr int ScreenHeight = 240;
constexpr int GraphLeft = 14;
constexpr int GraphTop = 100;
constexpr int GraphWidth = 292;
constexpr int GraphHeight = 112;
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

void StockRenderer::render(const char* symbol, const stock::Series* series,
                           const char* status, bool stale) {
  if (!ready_) {
    return;
  }

  char compactStatus[25]{};
  char detailStatus[47]{};
  std::snprintf(compactStatus, sizeof(compactStatus), "%.24s", status);
  std::snprintf(detailStatus, sizeof(detailStatus), "%.46s", status);

  canvas_.fillSprite(Background);
  canvas_.fillRect(0, 0, ScreenWidth, 30, HeaderBackground);
  canvas_.setTextDatum(textdatum_t::top_left);
  canvas_.setTextColor(TFT_WHITE);
  canvas_.setTextSize(2);
  canvas_.drawString(symbol, 12, 7);

  canvas_.setTextDatum(textdatum_t::top_right);
  canvas_.setTextSize(1);
  canvas_.setTextColor(stale ? TFT_ORANGE : TFT_LIGHTGREY);
  canvas_.drawString(stale ? "STALE" : compactStatus, ScreenWidth - 10, 10);

  if (series == nullptr || series->count == 0) {
    canvas_.setTextDatum(textdatum_t::middle_center);
    canvas_.setTextColor(TFT_WHITE);
    canvas_.setTextSize(2);
    canvas_.drawString(compactStatus, ScreenWidth / 2, ScreenHeight / 2 - 8);
    canvas_.setTextSize(1);
    canvas_.setTextColor(TFT_LIGHTGREY);
    canvas_.drawString("Waiting for market data", ScreenWidth / 2,
                       ScreenHeight / 2 + 20);
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
  canvas_.drawString(firstDate, GraphLeft, ScreenHeight - 5);
  canvas_.setTextDatum(textdatum_t::bottom_right);
  canvas_.drawString(lastDate, GraphLeft + GraphWidth, ScreenHeight - 5);
  canvas_.pushSprite(0, 0);
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