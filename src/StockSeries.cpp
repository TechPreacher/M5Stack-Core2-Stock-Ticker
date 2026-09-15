#include "StockSeries.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace stock {

bool addPoint(Series& series, const char* timestamp, float close) {
  if (series.count >= MaxPoints || timestamp == nullptr || !std::isfinite(close) ||
      close <= 0.0F) {
    return false;
  }

  const std::size_t timestampLength = std::strlen(timestamp);
  if (timestampLength != 10 && timestampLength != 19) {
    return false;
  }

  PricePoint& point = series.points[series.count++];
  std::snprintf(point.timestamp, sizeof(point.timestamp), "%s", timestamp);
  point.close = close;
  return true;
}

void sortAndKeepLatestTradingDays(Series& series, std::size_t tradingDays) {
  if (series.count == 0 || tradingDays == 0) {
    series.count = 0;
    return;
  }

  std::sort(series.points, series.points + series.count,
            [](const PricePoint& left, const PricePoint& right) {
              return std::strcmp(left.timestamp, right.timestamp) < 0;
            });

  std::size_t start = 0;
  std::size_t distinctDays = 0;
  char newestDate[11]{};
  for (std::size_t index = series.count; index > 0; --index) {
    const PricePoint& point = series.points[index - 1];
    if (distinctDays == 0 || std::strncmp(point.timestamp, newestDate, 10) != 0) {
      ++distinctDays;
      std::snprintf(newestDate, sizeof(newestDate), "%.10s", point.timestamp);
      if (distinctDays > tradingDays) {
        start = index;
        break;
      }
    }
  }

  if (start == 0) {
    return;
  }

  const std::size_t keptCount = series.count - start;
  std::move(series.points + start, series.points + series.count, series.points);
  series.count = keptCount;
}

Trend calculateTrend(const Series& series) {
  if (series.count < 2) {
    return Trend::Flat;
  }

  const float change = series.points[series.count - 1].close - series.points[0].close;
  constexpr float Epsilon = 0.0001F;
  if (change > Epsilon) {
    return Trend::Up;
  }
  if (change < -Epsilon) {
    return Trend::Down;
  }
  return Trend::Flat;
}

float calculatePercentageChange(const Series& series) {
  if (series.count < 2 || series.points[0].close <= 0.0F) {
    return 0.0F;
  }
  return ((series.points[series.count - 1].close / series.points[0].close) - 1.0F) *
         100.0F;
}

PriceRange calculatePriceRange(const Series& series) {
  if (series.count == 0) {
    return {};
  }

  float minimum = series.points[0].close;
  float maximum = minimum;
  for (std::size_t index = 1; index < series.count; ++index) {
    minimum = std::min(minimum, series.points[index].close);
    maximum = std::max(maximum, series.points[index].close);
  }

  const float spread = maximum - minimum;
  const float padding = spread > 0.0001F
                            ? spread * 0.05F
                            : std::max(std::fabs(maximum) * 0.01F, 0.01F);
  return {minimum - padding, maximum + padding};
}

}  // namespace stock