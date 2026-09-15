#pragma once

#include <cstddef>

namespace stock {

inline constexpr std::size_t MaxPoints = 100;

struct PricePoint {
  char timestamp[20]{};
  float close = 0.0F;
};

struct Series {
  PricePoint points[MaxPoints]{};
  std::size_t count = 0;
};

enum class Trend { Down, Flat, Up };

struct PriceRange {
  float minimum = 0.0F;
  float maximum = 1.0F;
};

bool addPoint(Series& series, const char* timestamp, float close);
void sortAndKeepLatestTradingDays(Series& series, std::size_t tradingDays);
Trend calculateTrend(const Series& series);
float calculatePercentageChange(const Series& series);
PriceRange calculatePriceRange(const Series& series);

}  // namespace stock