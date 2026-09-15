#pragma once

#include <M5Unified.h>

#include "StockSeries.h"

class StockRenderer {
 public:
  StockRenderer();

  bool begin();
  void render(const char* symbol, const stock::Series* series,
              const char* status, bool stale);

 private:
  void drawGraph(const stock::Series& series, std::uint16_t color);

  M5Canvas canvas_;
  bool ready_ = false;
};